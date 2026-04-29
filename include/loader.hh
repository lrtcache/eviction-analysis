#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

#include "traces.hh"

template <typename T>
concept IsTraceLoader = requires(T t) {
	{ t.done() } -> std::same_as<bool>;
	{ t.get() } -> std::same_as<const TraceEntry&>;
};

class SpanLoader
{
	TraceView view_;
	size_t cursor_;

public:
	SpanLoader(TraceView view) : view_{view}, cursor_{0} {}

	bool done() { return cursor_ >= view_.size(); }
	const TraceEntry& get()
	{
		const TraceEntry& entry = view_[cursor_];
		cursor_++;
		return entry;
	}
};

namespace fs = std::filesystem;
class FileLoader
{
	static constexpr size_t BATCH_SIZE{4 * 1024 * 1024};
	static constexpr size_t ENTRY_SIZE{sizeof(TraceEntry)};
	static constexpr size_t LOOKAHEAD_SIZE{64 * 1024 * 1024};
	fs::path filename_;
	int fd_{-1};
	size_t filesize_{};
	Trace buffer_{};
	size_t entries_in_buffer_{};
	size_t cursor_{};
	size_t file_offset_{};

public:
	explicit FileLoader(fs::path filename)
	    : filename_{std::move(filename)}, filesize_{fs::file_size(filename_)}
	{
		if (filesize_ % ENTRY_SIZE != 0) {
			throw std::runtime_error(std::format("trace file size must be a multiple of "
			                                     "TraceEntry size (which is {}), got {}",
			                                     sizeof(TraceEntry), filesize_));
		}

		fd_ = ::open(filename_.c_str(), O_RDONLY);
		if (fd_ < 0) {
			throw std::system_error(errno, std::generic_category(),
			                        "failed to open trace file " + filename_.string());
		}

		buffer_.resize(BATCH_SIZE / ENTRY_SIZE);
		entries_in_buffer_ = 0;
		advise(POSIX_FADV_SEQUENTIAL, 0, 0);
		// advise(POSIX_FADV_WILLNEED, 0, std::min(filesize_, LOOKAHEAD_SIZE));
	}

	FileLoader(const FileLoader&) = delete;
	FileLoader& operator=(const FileLoader&) = delete;

	FileLoader(FileLoader&& other) noexcept
	    : filename_{std::move(other.filename_)}, fd_{other.fd_},
	      filesize_{other.filesize_}, buffer_{std::move(other.buffer_)},
	      entries_in_buffer_{other.entries_in_buffer_}, cursor_{other.cursor_},
	      file_offset_{other.file_offset_}
	{
		other.fd_ = -1;
		other.filesize_ = 0;
		other.entries_in_buffer_ = 0;
		other.cursor_ = 0;
		other.file_offset_ = 0;
	}

	FileLoader& operator=(FileLoader&& other) noexcept
	{
		if (this == &other) {
			return *this;
		}

		if (fd_ >= 0) {
			::close(fd_);
		}

		filename_ = std::move(other.filename_);
		fd_ = other.fd_;
		filesize_ = other.filesize_;
		buffer_ = std::move(other.buffer_);
		entries_in_buffer_ = other.entries_in_buffer_;
		cursor_ = other.cursor_;
		file_offset_ = other.file_offset_;

		other.fd_ = -1;
		other.filesize_ = 0;
		other.entries_in_buffer_ = 0;
		other.cursor_ = 0;
		other.file_offset_ = 0;
		return *this;
	}

	~FileLoader()
	{
		if (fd_ >= 0) {
			::close(fd_);
		}
	}

	void advise(int advice, size_t offset, size_t length) const noexcept
	{
		const int rc = ::posix_fadvise(fd_, static_cast<off_t>(offset),
		                               static_cast<off_t>(length), advice);
		static_cast<void>(rc);
	}

	void refill()
	{
		const size_t bytes_to_read = std::min(BATCH_SIZE, filesize_ - file_offset_);
		const size_t entries_to_read = bytes_to_read / ENTRY_SIZE;
		size_t total_read = 0;
		auto* raw_buffer = reinterpret_cast<std::byte*>(buffer_.data());

		while (total_read < bytes_to_read) {
			const ssize_t bytes_read =
			    ::pread(fd_, raw_buffer + total_read, bytes_to_read - total_read,
			            static_cast<off_t>(file_offset_ + total_read));
			if (bytes_read < 0) {
				throw std::system_error(errno, std::generic_category(),
				                        "failed to read trace file " +
				                            filename_.string());
			}
			if (bytes_read == 0) {
				throw std::runtime_error("unexpected EOF while reading trace file " +
				                         filename_.string());
			}
			total_read += static_cast<size_t>(bytes_read);
		}

		// advise(POSIX_FADV_WILLNEED, file_offset_ + total_read,
		//        std::min(LOOKAHEAD_SIZE, filesize_ - std::min(filesize_, file_offset_ +
		//        total_read)));
		// advise(POSIX_FADV_DONTNEED, file_offset_, total_read);

		file_offset_ += total_read;
		entries_in_buffer_ = entries_to_read;
		cursor_ = 0;
	}

	bool done() { return file_offset_ >= filesize_ && cursor_ == entries_in_buffer_; }

	const TraceEntry& get()
	{
		if (done()) {
			throw std::out_of_range("trace loader exhausted");
		}

		if (cursor_ >= entries_in_buffer_) {
			refill();
		}

		const TraceEntry& res = buffer_[cursor_];
		cursor_++;
		return res;
	}
};

static_assert(IsTraceLoader<SpanLoader>);
static_assert(IsTraceLoader<FileLoader>);
