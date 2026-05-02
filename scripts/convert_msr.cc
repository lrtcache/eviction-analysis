#include "csv.hpp"
#include <charconv>
#include <cstdio>
#include <string>
#include <vector>
#include <format>

using namespace csv;
using namespace std;

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

enum class AccessMode : std::uint8_t
{
	Read = 0,
	Write = 1,
};

struct TraceEntry
{
	AccessMode mode{AccessMode::Read};
	uint64_t page_id{};
};

uint64_t get_page(uint64_t offset)
{
	constexpr uint64_t PAGE_SHIFT{12};
	return offset >> PAGE_SHIFT;
}

uint64_t make_pageid(uint64_t disknum, uint64_t page)
{
	return (disknum << 60) | page;
}

int main(int argc, char** argv)
{
	constexpr size_t WRITE_BATCH_SIZE{1 << 16};

	for (int i{1}; i < argc; ++i) {
		string filename{argv[i]};
		auto outname = filename + ".parsed";
		auto f = fopen(outname.c_str(), "wb");
		if (f == nullptr) {
			perror("fopen");
			cerr << "cannot open " << filename << '\n';
			return 1;
		}

		setvbuf(f, nullptr, _IOFBF, 1 << 20);
		vector<TraceEntry> batch;
		batch.reserve(WRITE_BATCH_SIZE);
		auto flush_batch = [&]() {
			if (batch.empty()) {
				return;
			}
			const auto written =
			    fwrite(batch.data(), sizeof(TraceEntry), batch.size(), f);
			if (written != batch.size()) {
				perror("fwrite");
				abort();
			}
			batch.clear();
		};

		uint64_t max_page{0}, max_dsknum{0}, num_pages{0};
		unordered_set<uint64_t> wset;
		CSVReader reader(filename, CSVFormat{}.no_header());
		for (const auto& row : reader) {
			// Timestamp, Hostname, DiskNumber, Type, Offset, Size, ResponseTime
			auto disk_num = parse_u64(row[2].get_sv());
			auto type = row[3].get_sv() == "Read" ? AccessMode::Read : AccessMode::Write;
			auto offset = parse_u64(row[4].get_sv());
			auto size = parse_u64(row[5].get_sv());
			auto start_pg = get_page(offset);
			auto end_pg = get_page(offset + size);
			for (auto page{start_pg}; page <= end_pg; ++page) {
				auto page_id = make_pageid(disk_num, page);
				batch.push_back({.mode = type, .page_id = page_id});
				if (batch.size() == WRITE_BATCH_SIZE) {
					flush_batch();
				}
				wset.insert(page_id);
			}
			num_pages += end_pg - start_pg;
			max_page = max(max_page, end_pg);
			max_dsknum = max(max_dsknum, disk_num);
		}
		cout << format("{} ->\n", filename) +
		            format("\tmax disk number: {}\n", max_dsknum) +
		            format("\tmax page: {}\n", max_page) +
		            format("\tnum pages: {}\n", num_pages) +
		            format("\tworking set size: {}\n", wset.size());
		flush_batch();
		fclose(f);
	}
	return 0;
}
