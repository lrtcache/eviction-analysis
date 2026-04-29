#pragma once

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <variant>

#include "traces.hh"

namespace fs = std::filesystem;
using std::size_t;
using std::string;
using std::string_view;
using std::unordered_map;
using std::vector;

struct FileTraceSpec
{
	fs::path path{};
};

using TraceSource = std::variant<Trace, FileTraceSpec>;

struct BenchmarkSpec
{
	string name;
	size_t capacity{};
	TraceSource trace{};
};

namespace benchmark_config
{

inline string trim(string_view input)
{
	size_t start = 0;
	while (start < input.size() &&
	       std::isspace(static_cast<unsigned char>(input[start]))) {
		start++;
	}

	size_t end = input.size();
	while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
		end--;
	}

	return string{input.substr(start, end - start)};
}

inline bool is_comment_or_empty(string_view line)
{
	const string trimmed = trim(line);
	return trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';';
}

inline long long parse_integer(string_view raw, string_view key, string_view section_name)
{
	const string text = trim(raw);
	size_t pos = 0;
	long long value{};

	try {
		value = std::stoll(text, &pos, 10);
	} catch (const std::exception&) {
		throw std::runtime_error("section [" + string{section_name} + "]: key '" +
		                         string{key} + "' must be an integer");
	}

	if (pos != text.size()) {
		throw std::runtime_error("section [" + string{section_name} + "]: key '" +
		                         string{key} + "' must be an integer");
	}

	return value;
}

inline uint64_t parse_u64(string_view raw, string_view key, string_view section_name)
{
	const long long value = parse_integer(raw, key, section_name);
	if (value < 0) {
		throw std::runtime_error("section [" + string{section_name} + "]: key '" +
		                         string{key} + "' must be nonnegative");
	}
	return static_cast<uint64_t>(value);
}

inline int parse_int(string_view raw, string_view key, string_view section_name)
{
	const long long value = parse_integer(raw, key, section_name);
	if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
	    value > static_cast<long long>(std::numeric_limits<int>::max())) {
		throw std::runtime_error("section [" + string{section_name} + "]: key '" +
		                         string{key} + "' is out of range for int");
	}
	return static_cast<int>(value);
}

inline double parse_double(string_view raw, string_view key, string_view section_name)
{
	const string text = trim(raw);
	size_t pos = 0;
	double value{};

	try {
		value = std::stod(text, &pos);
	} catch (const std::exception&) {
		throw std::runtime_error("section [" + string{section_name} + "]: key '" +
		                         string{key} + "' must be a number");
	}

	if (pos != text.size()) {
		throw std::runtime_error("section [" + string{section_name} + "]: key '" +
		                         string{key} + "' must be a number");
	}

	return value;
}

inline string require_value(const unordered_map<string, string>& kv, string_view key,
                            string_view section_name)
{
	const auto it = kv.find(string{key});
	if (it == kv.end()) {
		throw std::runtime_error("section [" + string{section_name} + "]: missing key '" +
		                         string{key} + "'");
	}
	return it->second;
}

inline string optional_value(const unordered_map<string, string>& kv, string_view key,
                             string_view fallback)
{
	const auto it = kv.find(string{key});
	if (it == kv.end()) {
		return string{fallback};
	}
	return it->second;
}

inline TraceSource build_trace(string_view section_name,
                               const unordered_map<string, string>& kv)
{
	const string kind = require_value(kv, "kind", section_name);

	if (kind == "random") {
		return make_random_read_trace(
		    static_cast<size_t>(parse_u64(require_value(kv, "length", section_name),
		                                  "length", section_name)),
		    parse_int(require_value(kv, "page_domain", section_name), "page_domain",
		              section_name),
		    parse_u64(optional_value(kv, "seed", "0"), "seed", section_name));
	}

	if (kind == "range") {
		return make_range_read_trace(
		    parse_int(require_value(kv, "start_page", section_name), "start_page",
		              section_name),
		    parse_int(require_value(kv, "page_count", section_name), "page_count",
		              section_name),
		    static_cast<size_t>(parse_u64(optional_value(kv, "repetitions", "1"),
		                                  "repetitions", section_name)));
	}

	if (kind == "sampled_range") {
		return make_sampled_range_read_trace(
		    parse_int(require_value(kv, "page_domain", section_name), "page_domain",
		              section_name),
		    static_cast<size_t>(parse_u64(require_value(kv, "segments", section_name),
		                                  "segments", section_name)),
		    parse_double(optional_value(kv, "alpha", "1.2"), "alpha", section_name),
		    parse_u64(optional_value(kv, "seed", "0"), "seed", section_name));
	}

	if (kind == "mixed") {
		return make_mixed_read_trace(
		    static_cast<size_t>(
		        parse_u64(require_value(kv, "random_prefix_length", section_name),
		                  "random_prefix_length", section_name)),
		    parse_int(require_value(kv, "random_page_domain", section_name),
		              "random_page_domain", section_name),
		    parse_int(require_value(kv, "range_start_page", section_name),
		              "range_start_page", section_name),
		    parse_int(require_value(kv, "range_page_count", section_name),
		              "range_page_count", section_name),
		    static_cast<size_t>(parse_u64(optional_value(kv, "range_repetitions", "1"),
		                                  "range_repetitions", section_name)),
		    parse_u64(optional_value(kv, "seed", "0"), "seed", section_name));
	}

	if (kind == "hotset_scan") {
		return make_hotset_scan_trace(
		    parse_int(require_value(kv, "hotset_start_page", section_name),
		              "hotset_start_page", section_name),
		    parse_int(require_value(kv, "hotset_size", section_name), "hotset_size",
		              section_name),
		    static_cast<size_t>(
		        parse_u64(require_value(kv, "hotset_rounds_before_scan", section_name),
		                  "hotset_rounds_before_scan", section_name)),
		    parse_int(require_value(kv, "scan_start_page", section_name),
		              "scan_start_page", section_name),
		    parse_int(require_value(kv, "scan_length", section_name), "scan_length",
		              section_name),
		    static_cast<size_t>(
		        parse_u64(require_value(kv, "hotset_rounds_after_scan", section_name),
		                  "hotset_rounds_after_scan", section_name)));
	}

	if (kind == "hot_cold_burst") {
		return make_hot_cold_burst_trace(
		    parse_int(require_value(kv, "hotset_start_page", section_name),
		              "hotset_start_page", section_name),
		    parse_int(require_value(kv, "hotset_size", section_name), "hotset_size",
		              section_name),
		    static_cast<size_t>(
		        parse_u64(require_value(kv, "hot_repetitions_per_burst", section_name),
		                  "hot_repetitions_per_burst", section_name)),
		    parse_int(require_value(kv, "coldset_start_page", section_name),
		              "coldset_start_page", section_name),
		    parse_int(require_value(kv, "coldset_size", section_name), "coldset_size",
		              section_name),
		    static_cast<size_t>(parse_u64(require_value(kv, "bursts", section_name),
		                                  "bursts", section_name)));
	}

	if (kind == "zipf") {
		return make_zipf_read_trace(
		    static_cast<size_t>(parse_u64(require_value(kv, "length", section_name),
		                                  "length", section_name)),
		    parse_int(require_value(kv, "page_domain", section_name), "page_domain",
		              section_name),
		    parse_double(require_value(kv, "alpha", section_name), "alpha", section_name),
		    parse_u64(optional_value(kv, "seed", "0"), "seed", section_name));
	}

	if (kind == "binary_file") {
		return FileTraceSpec{
		    .path = require_value(kv, "filename", section_name),
		};
	}

	throw std::runtime_error("section [" + string{section_name} + "]: unknown kind '" +
	                         kind + "'");
}

inline vector<BenchmarkSpec> load_benchmark_specs(const string& path)
{
	std::ifstream input{path};
	if (!input) {
		throw std::runtime_error("failed to open benchmark config '" + path + "'");
	}

	vector<BenchmarkSpec> specs{};
	string line{};
	string current_section{};
	unordered_map<string, string> current_values{};

	auto flush_section = [&]() {
		if (current_section.empty()) {
			return;
		}

		BenchmarkSpec spec{};
		spec.name = current_section;
		spec.capacity = static_cast<size_t>(
		    parse_u64(require_value(current_values, "capacity", current_section),
		              "capacity", current_section));
		spec.trace = build_trace(current_section, current_values);
		specs.push_back(std::move(spec));
		current_section.clear();
		current_values.clear();
	};

	size_t lineno = 0;
	while (std::getline(input, line)) {
		lineno++;
		const string trimmed = trim(line);
		if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
			continue;
		}

		if (trimmed.front() == '[') {
			if (trimmed.back() != ']') {
				throw std::runtime_error("line " + std::to_string(lineno) +
				                         ": invalid section header");
			}

			flush_section();
			current_section = trim(string_view{trimmed}.substr(1, trimmed.size() - 2));
			if (current_section.empty()) {
				throw std::runtime_error("line " + std::to_string(lineno) +
				                         ": empty section name");
			}
			continue;
		}

		if (current_section.empty()) {
			throw std::runtime_error("line " + std::to_string(lineno) +
			                         ": key/value pair outside any section");
		}

		const size_t eq = trimmed.find('=');
		if (eq == string::npos) {
			throw std::runtime_error("line " + std::to_string(lineno) +
			                         ": expected key = value");
		}

		const string key = trim(string_view{trimmed}.substr(0, eq));
		const string value = trim(string_view{trimmed}.substr(eq + 1));
		if (key.empty()) {
			throw std::runtime_error("line " + std::to_string(lineno) + ": empty key");
		}
		if (current_values.contains(key)) {
			throw std::runtime_error("section [" + current_section +
			                         "]: duplicate key '" + key + "'");
		}
		current_values.insert({key, value});
	}

	flush_section();

	if (specs.empty()) {
		throw std::runtime_error("benchmark config '" + path +
		                         "' does not define any sections");
	}

	return specs;
}

} // namespace benchmark_config
