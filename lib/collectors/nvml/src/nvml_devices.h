#pragma once

#include "nvml.h"

#include <cstddef>
#include <iterator>

namespace atlasagent
{

struct NvmlIndexedDevice
{
    unsigned int index;
    NvmlDeviceHandle handle;
};

// Iterates a count snapshot, skipping indices whose handles cannot be read.
template <typename Lib>
class NvmlDevices
{
   public:
    class Iterator
    {
       public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = NvmlIndexedDevice;
        using difference_type = std::ptrdiff_t;

        Iterator() noexcept = default;
        Iterator(Lib* lib, unsigned int count, unsigned int index) noexcept : lib_(lib), count_(count), index_(index)
        {
            SkipUnavailable();
        }

        value_type operator*() const noexcept { return {index_, handle_}; }

        Iterator& operator++() noexcept
        {
            ++index_;
            SkipUnavailable();
            return *this;
        }

        void operator++(int) noexcept { ++*this; }

        bool operator==(const Iterator& other) const noexcept { return lib_ == other.lib_ && index_ == other.index_; }

       private:
        void SkipUnavailable() noexcept
        {
            while (index_ < count_ && !lib_->get_by_index(index_, &handle_))
            {
                ++index_;
            }
        }

        Lib* lib_{nullptr};
        unsigned int count_{0};
        unsigned int index_{0};
        NvmlDeviceHandle handle_{nullptr};
    };

    NvmlDevices(Lib& lib, unsigned int count) noexcept : lib_(&lib), count_(count) {}

    Iterator begin() const noexcept { return {lib_, count_, 0}; }
    Iterator end() const noexcept { return {lib_, count_, count_}; }

   private:
    Lib* lib_;
    unsigned int count_;
};

}  // namespace atlasagent
