all: convert_msr analyze_raw_trace analyze_locality

convert_msr: convert_msr.cc csv.hpp
	g++ -std=c++20 -O3 $< -I. -o $@

analyze_raw_trace: analyze_raw_trace.cc csv.hpp
	g++ -std=c++20 -O3 $< -I. -o $@

analyze_locality: analyze_locality.cc
	g++ -std=c++20 -O3 $< -o $@
