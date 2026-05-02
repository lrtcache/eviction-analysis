#include "csv.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace csv;
using namespace std;

namespace fs = std::filesystem;

uint64_t parse_u64(std::string_view sv)
{
	uint64_t res;
	auto [ptr, err] = from_chars(sv.data(), sv.data() + sv.size(), res);
	if (err != errc{}) {
		cerr << "error parsing " << sv << '\n';
		abort();
	}
	return res;
}

enum class Series : size_t
{
	Overall = 0,
	Read = 1,
	Write = 2,
};

enum class TraceFormat
{
	Auto,
	MSR,
	SPC,
};

string_view series_name(Series series)
{
	switch (series) {
	case Series::Overall:
		return "overall";
	case Series::Read:
		return "read";
	case Series::Write:
		return "write";
	}
	abort();
}

Series parse_mode(std::string_view sv)
{
	if (sv == "Read" || sv == "read" || sv == "R" || sv == "r") {
		return Series::Read;
	}
	if (sv == "Write" || sv == "write" || sv == "W" || sv == "w") {
		return Series::Write;
	}
	cerr << "unknown operation type: " << sv << '\n';
	abort();
}

TraceFormat detect_format(const fs::path& path, TraceFormat fmt)
{
	if (fmt != TraceFormat::Auto) {
		return fmt;
	}
	if (path.extension() == ".csv") {
		return TraceFormat::MSR;
	}
	if (path.extension() == ".spc") {
		return TraceFormat::SPC;
	}
	throw runtime_error(format("could not infer format for {}", path.string()));
}

uint64_t touched_pages(uint64_t offset, uint64_t size_bytes, uint64_t page_size)
{
	if (size_bytes == 0) {
		return 0;
	}
	const uint64_t first_page = offset / page_size;
	const uint64_t last_page = (offset + size_bytes - 1) / page_size;
	return last_page - first_page + 1;
}

struct Stats
{
	uint64_t requests{};
	uint64_t total_bytes{};
	uint64_t total_pages{};
	map<uint64_t, uint64_t> size_counts{};
	map<uint64_t, uint64_t> page_counts{};

	void add(uint64_t size_bytes, uint64_t pages)
	{
		requests++;
		total_bytes += size_bytes;
		total_pages += pages;
		size_counts[size_bytes]++;
		page_counts[pages]++;
	}
};

struct ParsedRow
{
	Series mode;
	uint64_t offset;
	uint64_t size_bytes;
};

ParsedRow parse_row(const CSVRow& row, TraceFormat fmt)
{
	switch (fmt) {
	case TraceFormat::MSR:
		return ParsedRow{
		    .mode = parse_mode(row[3].get_sv()),
		    .offset = parse_u64(row[4].get_sv()),
		    .size_bytes = parse_u64(row[5].get_sv()),
		};
	case TraceFormat::SPC:
		return ParsedRow{
		    .mode = parse_mode(row[3].get_sv()),
		    .offset = parse_u64(row[1].get_sv()),
		    .size_bytes = parse_u64(row[2].get_sv()),
		};
	case TraceFormat::Auto:
		break;
	}
	abort();
}

uint64_t quantile_from_counts(const map<uint64_t, uint64_t>& counts, uint64_t total,
                              double q)
{
	if (total == 0) {
		return 0;
	}
	const uint64_t rank = static_cast<uint64_t>(q * total + 0.999999999);
	uint64_t seen = 0;
	for (const auto& [value, freq] : counts) {
		seen += freq;
		if (seen >= max<uint64_t>(rank, 1)) {
			return value;
		}
	}
	return counts.rbegin()->first;
}

double bucket_fraction(const map<uint64_t, uint64_t>& counts, uint64_t total,
                       uint64_t lower, uint64_t upper)
{
	if (total == 0) {
		return 0.0;
	}
	uint64_t matched = 0;
	for (const auto& [value, freq] : counts) {
		if (value < lower || value > upper) {
			continue;
		}
		matched += freq;
	}
	return static_cast<double>(matched) / static_cast<double>(total);
}

double bucket_fraction_ge(const map<uint64_t, uint64_t>& counts, uint64_t total,
                          uint64_t lower)
{
	if (total == 0) {
		return 0.0;
	}
	uint64_t matched = 0;
	for (const auto& [value, freq] : counts) {
		if (value < lower) {
			continue;
		}
		matched += freq;
	}
	return static_cast<double>(matched) / static_cast<double>(total);
}

void write_summary_csv(const fs::path& output_path, const array<Stats, 3>& stats)
{
	ofstream out(output_path);
	out << "series,requests,share,total_bytes,mean_size_bytes,p50_size_bytes,"
	       "p90_size_bytes,p99_size_bytes,mean_pages_per_request,frac_1_page,"
	       "frac_2_4_pages,frac_5_16_pages,frac_17plus_pages\n";

	const double overall_requests =
	    static_cast<double>(stats[static_cast<size_t>(Series::Overall)].requests);
	for (auto series : {Series::Overall, Series::Read, Series::Write}) {
		const auto& s = stats[static_cast<size_t>(series)];
		const double requests = static_cast<double>(s.requests);
		const double share =
		    overall_requests > 0.0 ? requests / overall_requests : 0.0;
		const double mean_size =
		    requests > 0.0 ? static_cast<double>(s.total_bytes) / requests : 0.0;
		const double mean_pages =
		    requests > 0.0 ? static_cast<double>(s.total_pages) / requests : 0.0;

		out << format(
		    "{},{},{:.6f},{},{:.6f},{},{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f}\n",
		    series_name(series), s.requests, share, s.total_bytes, mean_size,
		    quantile_from_counts(s.size_counts, s.requests, 0.50),
		    quantile_from_counts(s.size_counts, s.requests, 0.90),
		    quantile_from_counts(s.size_counts, s.requests, 0.99), mean_pages,
		    bucket_fraction(s.page_counts, s.requests, 1, 1),
		    bucket_fraction(s.page_counts, s.requests, 2, 4),
		    bucket_fraction(s.page_counts, s.requests, 5, 16),
		    bucket_fraction_ge(s.page_counts, s.requests, 17));
	}
}

void write_distribution_csv(const fs::path& output_path, const array<Stats, 3>& stats,
                            bool use_size)
{
	ofstream out(output_path);
	out << "series,value,count,fraction,cdf\n";
	for (auto series : {Series::Overall, Series::Read, Series::Write}) {
		const auto& s = stats[static_cast<size_t>(series)];
		const auto& counts = use_size ? s.size_counts : s.page_counts;
		if (s.requests == 0) {
			continue;
		}
		uint64_t seen = 0;
		for (const auto& [value, count] : counts) {
			seen += count;
			const double fraction =
			    static_cast<double>(count) / static_cast<double>(s.requests);
			const double cdf =
			    static_cast<double>(seen) / static_cast<double>(s.requests);
			out << format("{},{},{},{:.12f},{:.12f}\n", series_name(series), value,
			              count, fraction, cdf);
		}
	}
}

void print_summary(const fs::path& path, const array<Stats, 3>& stats)
{
	const auto& overall = stats[static_cast<size_t>(Series::Overall)];
	cout << path.string() << '\n';
	for (auto series : {Series::Overall, Series::Read, Series::Write}) {
		const auto& s = stats[static_cast<size_t>(series)];
		const double share =
		    overall.requests > 0
		        ? static_cast<double>(s.requests) / static_cast<double>(overall.requests)
		        : 0.0;
		const double mean_size =
		    s.requests > 0
		        ? static_cast<double>(s.total_bytes) / static_cast<double>(s.requests)
		        : 0.0;
		const double mean_pages =
		    s.requests > 0
		        ? static_cast<double>(s.total_pages) / static_cast<double>(s.requests)
		        : 0.0;
		cout << format(
		    "  {:>7}: requests={} share={:.3f} mean_size={:.1f}B p50={}B p90={}B "
		    "mean_pages={:.2f}\n",
		    series_name(series), s.requests, share, mean_size,
		    quantile_from_counts(s.size_counts, s.requests, 0.50),
		    quantile_from_counts(s.size_counts, s.requests, 0.90), mean_pages);
		cout << format(
		    "           page buckets: 1={:.3f}, 2-4={:.3f}, 5-16={:.3f}, 17+={:.3f}\n",
		    bucket_fraction(s.page_counts, s.requests, 1, 1),
		    bucket_fraction(s.page_counts, s.requests, 2, 4),
		    bucket_fraction(s.page_counts, s.requests, 5, 16),
		    bucket_fraction_ge(s.page_counts, s.requests, 17));
	}
}

vector<fs::path> expand_inputs(const vector<string>& inputs)
{
	vector<fs::path> res;
	for (const auto& raw : inputs) {
		fs::path path{raw};
		if (fs::is_directory(path)) {
			for (const auto& entry : fs::directory_iterator(path)) {
				const auto ext = entry.path().extension();
				if (ext == ".csv" || ext == ".spc") {
					res.push_back(entry.path());
				}
			}
			continue;
		}
		if (fs::exists(path)) {
			res.push_back(path);
		}
	}
	sort(res.begin(), res.end());
	res.erase(unique(res.begin(), res.end()), res.end());
	return res;
}

int main(int argc, char** argv)
{
	TraceFormat input_format = TraceFormat::Auto;
	uint64_t page_size = 4096;
	fs::path output_dir;
	bool has_output_dir = false;
	vector<string> inputs;

	for (int i = 1; i < argc; ++i) {
		const string arg = argv[i];
		if (arg == "--format") {
			if (++i >= argc) {
				cerr << "--format requires a value\n";
				return 1;
			}
			const string value = argv[i];
			if (value == "auto") {
				input_format = TraceFormat::Auto;
			} else if (value == "msr") {
				input_format = TraceFormat::MSR;
			} else if (value == "spc") {
				input_format = TraceFormat::SPC;
			} else {
				cerr << "unknown format: " << value << '\n';
				return 1;
			}
		} else if (arg == "--page-size") {
			if (++i >= argc) {
				cerr << "--page-size requires a value\n";
				return 1;
			}
			page_size = stoull(argv[i]);
		} else if (arg == "--output-dir") {
			if (++i >= argc) {
				cerr << "--output-dir requires a value\n";
				return 1;
			}
			output_dir = argv[i];
			has_output_dir = true;
		} else {
			inputs.push_back(arg);
		}
	}

	if (inputs.empty()) {
		cerr << "usage: analyze_raw_trace [--format auto|msr|spc] [--page-size N] "
		        "[--output-dir DIR] <trace paths...>\n";
		return 1;
	}

	if (has_output_dir) {
		fs::create_directories(output_dir);
	}

	const auto paths = expand_inputs(inputs);
	if (paths.empty()) {
		cerr << "no trace files found\n";
		return 1;
	}

	for (const auto& path : paths) {
		const auto fmt = detect_format(path, input_format);
		array<Stats, 3> stats{};

		CSVReader reader(path.string(), CSVFormat{}.no_header());
		for (const auto& row : reader) {
			const auto parsed = parse_row(row, fmt);
			const auto pages = touched_pages(parsed.offset, parsed.size_bytes, page_size);
			stats[static_cast<size_t>(Series::Overall)].add(parsed.size_bytes, pages);
			stats[static_cast<size_t>(parsed.mode)].add(parsed.size_bytes, pages);
		}

		print_summary(path, stats);

		if (!has_output_dir) {
			continue;
		}

		const auto stem = path.stem().string();
		write_summary_csv(output_dir / format("{}-summary.csv", stem), stats);
		write_distribution_csv(output_dir / format("{}-request-size-dist.csv", stem),
		                       stats, true);
		write_distribution_csv(output_dir / format("{}-pages-per-request-dist.csv", stem),
		                       stats, false);
	}

	return 0;
}
