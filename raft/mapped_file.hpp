//
// Copyright (c) 2023-present DeepGrace (complex dot invoke at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/deepgrace/draft
//

#ifndef MAPPED_FILE_HPP
#define MAPPED_FILE_HPP

#include <string>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

static constexpr size_t page_size = 4096;

enum rc_errno
{
    RC_NOT_LEADER = -10,
    RC_CONF_ERROR,
    RC_MMAP_NEW_FILE,
    RC_MMAP_NOT_MAPPED,
    RC_MMAP_ALREADY_MAPPED,
    RC_MMAP_INVALID_FILE,
    RC_MMAP_ERROR,
    RC_OOR,
    RC_OOM,
    RC_ERROR,
    RC_GOOD
};

namespace draft
{
    class mapped_file 
    {
    public:
        mapped_file(const mapped_file&) = delete;
        mapped_file& operator=(const mapped_file&) = delete;

        template <typename T>
        mapped_file(T&& t, size_t size = 0, size_t length = 10, off_t off = 0, void* hint = nullptr) : size_(size), length(length), off(off), hint(hint)
        {
            using type = std::remove_cvref_t<T>;

            if constexpr(std::is_same_v<type, int>)
                fd_ = t;
            else if constexpr(std::is_same_v<type, std::string> || std::is_same_v<type, char*>)
                filename_ = t;
        }

        rc_errno map()
        {
            bool created = false;

            if (mapped)
                return RC_MMAP_ALREADY_MAPPED;

            if (filename_.empty() && fd_ < 0)
            {
                LOG_ERROR << "failied in map: filename is empty and file descriptor not specified."; 

                return RC_MMAP_INVALID_FILE;
            }

            if (fd_ < 0)
            {
                if (fd_ = open(filename_.c_str(), O_RDWR, 0644); fd_ == -1 && errno == ENOENT)
                {
                    if (fd_ = open(filename_.c_str(), O_RDWR | O_CREAT, 0644); fd_ == -1)
                    {
                        LOG_ERROR << "failed to create file " << filename_ << " " << std::strerror(errno);

                        return RC_MMAP_INVALID_FILE;
                    }

                    size_ = (size_ + page_size - 1) & ~(page_size - 1);
                    ftruncate(fd_, size_);

                    created = true;
                }
            }
            else
            {
                #ifndef F_GETPATH
                    filename_ = "unknown";
                #else
                    char filepath[1024];

                    if (fcntl(fd_, F_GETPATH, filepath) == -1)
                    {
                        LOG_ERROR << "failed to get file path of fd " << fd_ << " " << std::strerror(errno);

                        return RC_MMAP_ERROR;
                    }

                    filename_ = filepath;
                #endif 
            }

            if (size_ == 0)
            {
                struct stat st;

                if (fstat(fd_, &st) == -1)
                {
                    LOG_ERROR << "failed to fstat() on file " << filename_ << " " << std::strerror(errno);

                    return RC_MMAP_ERROR;
                }

                size_ = st.st_size;
                size_ = (size_ + page_size - 1) & ~(page_size - 1);

                if (!size_)
                {
                    size_ = length;
                    size_ = (size_ + page_size - 1) & ~(page_size - 1);

                    ftruncate(fd_, size_);
                }
            }

            addr_ = mmap(hint, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, off);

            if (addr_ == MAP_FAILED)
            {
                LOG_ERROR << "failed to mmap() " << std::strerror(errno);

                return RC_MMAP_ERROR;
            }

            mapped = true;

            if (created)
            {
                std::memset(addr_, 0, size_);

                return RC_MMAP_NEW_FILE;
            }

            return RC_GOOD;
        }

        rc_errno unmap()
        {
            if (!mapped)
                return RC_GOOD;

            LOG_INFO << "unmmapping : " << addr_ << " , " << size_;

            if (munmap(addr_, size_) == -1)
            {
                LOG_ERROR << "failed to munmap() " << std::strerror(errno);

                return RC_MMAP_ERROR;
            }

            mapped = false;

            return RC_GOOD;
        }

        rc_errno remap(size_t new_size)
        {
            if (!mapped)
            {
                LOG_ERROR << "the file is not yet mapped";

                return RC_MMAP_NOT_MAPPED;
            }

            void* new_addr;
            new_size = (new_size + page_size - 1) & ~(page_size - 1);

            #ifdef HAVE_MREMAP
                if (new_addr = mremap(addr_, size_, new_size, MREMAP_MAYMOVE); new_addr == MAP_FAILED)
                {
                    LOG_ERROR << "failed to mremap() " << std::strerror(errno);

                    return RC_MMAP_ERROR;
                }
            #else
                mapped = false;

                if (auto r = unmap(); r != RC_GOOD)
                    return r;

                LOG_INFO << "ftruncate : " << fd_ << " , " << new_size;

                if (ftruncate(fd_, new_size) == -1)
                {
                    LOG_ERROR << "failed to ftruncate() " << std::strerror(errno);

                    return RC_MMAP_ERROR;
                }

                LOG_INFO << "mmap: " << new_size << ", " << off;

                if (new_addr = mmap(hint, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, off); new_addr == MAP_FAILED)
                {
                    LOG_ERROR << "failed to mmap() " << std::strerror(errno);

                    if (errno == ENOMEM)
                        return RC_OOM;
                    else
                        return RC_MMAP_ERROR;
                }

                mapped = true;
            #endif

            addr_ = new_addr;
            size_ = new_size;

            return RC_GOOD;
        }

        rc_errno sync_all()
        {
            return sync_range(addr_, size_);
        }

        rc_errno sync_range(void* addr, size_t length)
        {
            if (!mapped)
            {
                LOG_ERROR << "the file is not yet mapped";

                return RC_MMAP_NOT_MAPPED;
            }

            uint64_t left = (uint64_t)addr & (page_size - 1);
            auto aligned_addr = reinterpret_cast<void*>((uint64_t)addr - left);

            if (msync(aligned_addr, length + left, MS_SYNC) == -1)
            {
                LOG_ERROR << "failed to msync() " << std::strerror(errno);

                return RC_MMAP_ERROR;
            }

            return RC_GOOD;
        }

        rc_errno advise(int advise)
        {
            if (!mapped)
            {
                LOG_ERROR << "the file is not yet mapped";

                return RC_MMAP_NOT_MAPPED;
            }

            if (madvise(addr_, size_, advise) == -1)
            {
                LOG_ERROR << "failed to madvise() " << std::strerror(errno);

                return RC_MMAP_ERROR;
            }

            return RC_GOOD;
        }

        size_t size()
        {
            return size_;
        }

        int fd()
        {
            return fd_;
        }

        std::string& filename()
        {
            return filename_;
        }

        void* addr()
        {
            return addr_;
        }

        ~mapped_file()
        {
            if (size_ > 0)
                unmap();
        }

    private:
        std::string filename_;
        int fd_ = -1;

        size_t size_;
        size_t length;

        off_t off;
        void* hint;

        bool mapped = false;
        void* addr_;
    };
}

#endif
