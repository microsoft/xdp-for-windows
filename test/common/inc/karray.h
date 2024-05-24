//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//

/*++

Abstract:

    Implements a C++ container analogous to std::vector

Environment:

    Kernel mode or usermode unittest

Notes:

    Because kernel C++ doesn't support exceptions, we can't use the STL directly
    in kernelmode.  Therefore, this class provides a limited and slightly
    modified subset of the STL's std::vector.

    If you're not familiar with std::vector, you should go read up on it
    first: https://docs.microsoft.com/en-us/cpp/standard-library/vector-class

    This file was originally copied from the following location and then
    modified to reduce dependencies:

    https://github.com/microsoft/Network-Adapter-Class-Extension/blob/windows_10.0.19541/ndis/rtl/inc/karray.h

--*/

#pragma once

#include "cxplat.h"
#include <new.h>
#ifdef KERNEL_MODE
#include <ntintsafe.h>
#else
#include <intsafe.h>
#define RtlSIZETMult SizeTMult
#endif
#include <wil/wistd_type_traits.h>

#define NOTHING

#if defined(KERNEL_MODE)
#define CODE_SEG(segment) __declspec(code_seg(segment))
#else
#define CODE_SEG(segment)
#endif

#ifndef KRTL_INIT_SEGMENT
#define KRTL_INIT_SEGMENT "INIT"
#endif
#ifndef KRTL_PAGE_SEGMENT
#define KRTL_PAGE_SEGMENT "PAGE"
#endif
#ifndef KRTL_NONPAGED_SEGMENT
#define KRTL_NONPAGED_SEGMENT ".text"
#endif

// Use on pageable functions.
#define PAGED CODE_SEG(KRTL_PAGE_SEGMENT) _IRQL_always_function_max_(PASSIVE_LEVEL)

// Use on code that must always be locked in memory.
#define NONPAGED CODE_SEG(KRTL_NONPAGED_SEGMENT) _IRQL_requires_max_(DISPATCH_LEVEL)

// Use on code that must always be locked in memory, where you don't want SAL IRQL annotations.
#define NONPAGEDX CODE_SEG(KRTL_NONPAGED_SEGMENT)

#ifndef _KERNEL_MODE

#ifndef PAGED_CODE
#define PAGED_CODE() (void)0
#endif // PAGED_CODE

#ifndef INIT_CODE
#define INIT_CODE() (void)0
#endif // INIT_CODE

#endif // _KERNEL_MODE

// Use on classes or structs.  Class member functions & compiler-generated code
// will default to the PAGE segment.  You can override any member function with `NONPAGED`.
#define KRTL_CLASS CODE_SEG(KRTL_PAGE_SEGMENT) __declspec(empty_bases)

// Use on classes or structs.  Class member functions & compiler-generated code
// will default to the NONPAGED segment.  You can override any member function with `PAGED`.
#define KRTL_CLASS_DPC_ALLOC __declspec(empty_bases)

template<ULONG SIGNATURE>
struct KRTL_CLASS DebugBlock
{
#if DBG
    PAGED ~DebugBlock()
    {
        ASSERT_VALID();
        Signature |= 0x80;
    }
#endif

    NONPAGED void ASSERT_VALID() const
    {
#if DBG
        CXPLAT_DBG_ASSERT(Signature == SIGNATURE);
#endif
    }

private:
#if DBG
    ULONG Signature = SIGNATURE;
#endif
};

#ifndef KERNEL_MODE

DECLSPEC_NORETURN
FORCEINLINE
VOID
RtlFailFast(
    _In_ ULONG Code
    )

{
    __fastfail(Code);
}


//
// Pool Allocation routines (in pool.c)
//
typedef _Enum_is_bitflag_ enum _POOL_TYPE {
    NonPagedPool,
    NonPagedPoolExecute = NonPagedPool,
    PagedPool,
    NonPagedPoolMustSucceed = NonPagedPool + 2,
    DontUseThisType,
    NonPagedPoolCacheAligned = NonPagedPool + 4,
    PagedPoolCacheAligned,
    NonPagedPoolCacheAlignedMustS = NonPagedPool + 6,
    MaxPoolType,

    //
    // Define base types for NonPaged (versus Paged) pool, for use in cracking
    // the underlying pool type.
    //

    NonPagedPoolBase = 0,
    NonPagedPoolBaseMustSucceed = NonPagedPoolBase + 2,
    NonPagedPoolBaseCacheAligned = NonPagedPoolBase + 4,
    NonPagedPoolBaseCacheAlignedMustS = NonPagedPoolBase + 6,

    //
    // Note these per session types are carefully chosen so that the appropriate
    // masking still applies as well as MaxPoolType above.
    //

    NonPagedPoolSession = 32,
    PagedPoolSession = NonPagedPoolSession + 1,
    NonPagedPoolMustSucceedSession = PagedPoolSession + 1,
    DontUseThisTypeSession = NonPagedPoolMustSucceedSession + 1,
    NonPagedPoolCacheAlignedSession = DontUseThisTypeSession + 1,
    PagedPoolCacheAlignedSession = NonPagedPoolCacheAlignedSession + 1,
    NonPagedPoolCacheAlignedMustSSession = PagedPoolCacheAlignedSession + 1,

    NonPagedPoolNx = 512,
    NonPagedPoolNxCacheAligned = NonPagedPoolNx + 4,
    NonPagedPoolSessionNx = NonPagedPoolNx + 32,

} POOL_TYPE;

#endif

//
//                                           KALLOCATOR           KALLOCATOR_NONPAGED
// ---------------------------------------+--------------------+--------------------+
//  The object must be allocated at IRQL: | = PASSIVE_LEVEL    |  = PASSIVE_LEVEL   |
// ---------------------------------------+--------------------+--------------------+
//  The object must be freed at IRQL:     | = PASSIVE_LEVEL    |  = PASSIVE_LEVEL   |
// ---------------------------------------+--------------------+--------------------+
//  Constructor & destructor run at:      | = PASSIVE_LEVEL    |  = PASSIVE_LEVEL   |
// ---------------------------------------+--------------------+--------------------+
//  Member functions default to:          | PAGED code segment | .text code segment |
// ---------------------------------------+--------------------+--------------------+
//  Compiler-generated code goes to:      | PAGED code segment | .text code segment |
// ---------------------------------------+--------------------+--------------------+
//  The memory is allocated from pool:    | paged or nonpaged  | paged or nonpaged  |
// ---------------------------------------+--------------------+--------------------+
//

PAGED void *operator new(size_t s, std::nothrow_t const &, ULONG tag);
PAGED void operator delete(void *p, ULONG tag);
PAGED void *operator new[](size_t s, std::nothrow_t const &, ULONG tag);
PAGED void operator delete[](void *p, ULONG tag);
PAGEDX void __cdecl operator delete[](void *p);
void __cdecl operator delete(void *p);

#ifndef _KERNEL_MODE

PVOID
ExAllocatePoolWithTag(
    POOL_TYPE PoolType,
    SIZE_T NumberOfBytes,
    ULONG Tag
    )
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(Tag);
    return malloc(NumberOfBytes);
}

VOID
ExFreePool(
    _Pre_notnull_ __drv_freesMem(Mem) PVOID P
    )
{
    free(P);
}

VOID
ExFreePoolWithTag(
    _Pre_notnull_ __drv_freesMem(Mem) PVOID P,
    _In_ ULONG Tag
    )
{
    UNREFERENCED_PARAMETER(Tag);
    ExFreePool(P);
}

#endif // _KERNEL_MODE

template <ULONG TAG, ULONG ARENA = PagedPool>
struct KRTL_CLASS KALLOCATION_TAG
{
    static const ULONG AllocationTag = TAG;
    static const ULONG AllocationArena = ARENA;
};

template <ULONG TAG, ULONG ARENA = NonPagedPoolNx>
struct KRTL_CLASS_DPC_ALLOC KALLOCATION_TAG_DPC_ALLOC
{
    static const ULONG AllocationTag = TAG;
    static const ULONG AllocationArena = ARENA;
};

template <ULONG TAG, ULONG ARENA = PagedPool>
struct KRTL_CLASS KALLOCATOR : public KALLOCATION_TAG<TAG, ARENA>
{
    // Scalar new & delete

    PAGED void *operator new(size_t cb, std::nothrow_t const &)
    {
        PAGED_CODE();
        return ExAllocatePoolWithTag(static_cast<POOL_TYPE>(ARENA), cb, TAG);
    }

    PAGED void operator delete(void *p)
    {
        PAGED_CODE();

        if (p != nullptr)
        {
            ExFreePoolWithTag(p, TAG);
        }
    }

    // Scalar new with bonus bytes

    PAGED void *operator new(size_t cb, std::nothrow_t const &, size_t extraBytes)
    {
        PAGED_CODE();

        auto size = cb + extraBytes;

        // Overflow check
        if (size < cb)
            return nullptr;

        return ExAllocatePoolWithTag(static_cast<POOL_TYPE>(ARENA), size, TAG);
    }

    // Array new & delete

    PAGED void *operator new[](size_t cb, std::nothrow_t const &)
    {
        PAGED_CODE();
        return ExAllocatePoolWithTag(static_cast<POOL_TYPE>(ARENA), cb, TAG);
    }

    PAGED void operator delete[](void *p)
    {
        PAGED_CODE();

        if (p != nullptr)
        {
            ExFreePoolWithTag(p, TAG);
        }
    }

    // Placement new & delete

    PAGED void *operator new(size_t n, void * p)
    {
        PAGED_CODE();
        UNREFERENCED_PARAMETER((n));
        return p;
    }

    PAGED void operator delete(void *p1, void *p2)
    {
        PAGED_CODE();
        UNREFERENCED_PARAMETER((p1, p2));
    }
};

template <ULONG TAG, ULONG ARENA = NonPagedPoolNx>
struct KRTL_CLASS_DPC_ALLOC KALLOCATOR_NONPAGED : public KALLOCATION_TAG_DPC_ALLOC<TAG, ARENA>
{
    // Scalar new & delete

    NONPAGED void *operator new(size_t cb, std::nothrow_t const &)
    {
        return ExAllocatePoolWithTag(static_cast<POOL_TYPE>(ARENA), cb, TAG);
    }

    NONPAGED void operator delete(void *p)
    {
        if (p != nullptr)
        {
            ExFreePoolWithTag(p, TAG);
        }
    }

    // Scalar new with bonus bytes

    NONPAGED void *operator new(size_t cb, std::nothrow_t const &, size_t extraBytes)
    {
        auto size = cb + extraBytes;

        // Overflow check
        if (size < cb)
            return nullptr;

        return ExAllocatePoolWithTag(static_cast<POOL_TYPE>(ARENA), size, TAG);
    }

    // Array new & delete

    NONPAGED void *operator new[](size_t cb, std::nothrow_t const &)
    {
        return ExAllocatePoolWithTag(static_cast<POOL_TYPE>(ARENA), cb, TAG);
    }

    NONPAGED void operator delete[](void *p)
    {
        if (p != nullptr)
        {
            ExFreePoolWithTag(p, TAG);
        }
    }

    // Placement new & delete

    NONPAGED void *operator new(size_t n, void * p)
    {
        UNREFERENCED_PARAMETER((n));
        return p;
    }

    NONPAGED void operator delete(void *p1, void *p2)
    {
        UNREFERENCED_PARAMETER((p1, p2));
    }
};

template <ULONG TAG>
struct KRTL_CLASS PAGED_OBJECT :
    public KALLOCATOR<TAG, PagedPool>,
    public DebugBlock<TAG>
{

};

template <ULONG TAG>
struct KRTL_CLASS NONPAGED_OBJECT :
    public KALLOCATOR<TAG, NonPagedPoolNx>,
    public DebugBlock<TAG>
{

};


class triageClass;
VOID initGlobalTriageBlock();

namespace Rtl
{

template<typename T, POOL_TYPE PoolType = PagedPool>
class KRTL_CLASS KArray :
    public PAGED_OBJECT<'rrAK'>
{
public:

    static_assert(((PoolType == NonPagedPoolNxCacheAligned) && (alignof(T) <= SYSTEM_CACHE_ALIGNMENT_SIZE)) ||
                  (alignof(T) <= MEMORY_ALLOCATION_ALIGNMENT),
                  "This container allocates items with a fixed alignment");

    // This iterator is not a full implementation of a STL-style iterator.
    // Mostly this is only here to get C++'s syntax "for(x : y)" to work.
    class const_iterator
    {
    friend class KArray;
    protected:

        PAGED const_iterator(KArray const *a, size_t i) : _a{ const_cast<KArray*>(a) }, _i{ i } { }

    public:

        const_iterator() = delete;
        PAGED const_iterator(const_iterator const &rhs) : _a { rhs._a }, _i{ rhs._i } { }
        PAGED ~const_iterator() = default;

        PAGED const_iterator &operator=(const_iterator const &rhs) { _a = rhs._a; _i = rhs._i; return *this; }

        PAGED const_iterator &operator++() { _i++; return *this; }
        PAGED const_iterator operator++(int) { auto result = *this; ++(*this); return result; }

        PAGED T const &operator*() const { return (*_a)[_i]; }
        PAGED T const *operator->() const { return &(*_a)[_i]; }

        PAGED bool operator==(const_iterator const &rhs) const { return rhs._i == _i; }
        PAGED bool operator!=(const_iterator const &rhs) const { return !(rhs == *this); }

    protected:

        KArray *_a;
        size_t _i;
    };

    class iterator : public const_iterator
    {
    friend class KArray;
    protected:

        PAGED iterator(KArray *a, size_t i) : const_iterator{ a, i } {}

    public:

        PAGED T &operator*() const { return (*this->_a)[this->_i]; }
        PAGED T *operator->() const { return &(*this->_a)[this->_i]; }
    };

    PAGED KArray(size_t sizeHint = 0, const T &value = (T)0) noexcept
    {
        if (sizeHint)
        {
            (void)grow(sizeHint);
            for (ULONG i = 0; i < m_numElements; i++)
                _p[i] = value;
        }
    }

    NONPAGED ~KArray()
    {
        reset();
    }

    PAGED KArray(
        _In_ KArray &&rhs) noexcept :
            _p(rhs._p),
            m_numElements(rhs.m_numElements),
            m_bufferSize(rhs.m_bufferSize)
    {
        rhs._p = nullptr;
        rhs.m_numElements = 0;
        rhs.m_bufferSize = 0;
    }

    KArray(KArray &) = delete;

    KArray &operator=(KArray &) = delete;

    PAGED KArray &operator=(
        _In_ KArray &&rhs) noexcept
    {
        reset();

        this->_p = rhs._p;
        this->m_numElements = rhs.m_numElements;
        this->m_bufferSize = rhs.m_bufferSize;

        rhs._p = nullptr;
        rhs.m_numElements = 0;
        rhs.m_bufferSize = 0;

        return *this;
    }

    NONPAGED size_t count() const
    {
        return m_numElements;
    }

    NONPAGED size_t size() const
    {
        return count();
    }

    PAGED bool reserve(size_t count)
    {
        if (m_bufferSize >= count)
            return true;

        if (count >= (ULONG)(-1))
            return false;

        size_t bytesNeeded;
        if (!NT_SUCCESS(RtlSIZETMult(sizeof(T), count, reinterpret_cast<SIZE_T*>(&bytesNeeded))))
            return false;

        T * p = (T*)ExAllocatePoolWithTag(PoolType, bytesNeeded, 'rrAK');
        if (!p)
            return false;

        if constexpr(__is_trivially_copyable(T))
        {
            memcpy(p, _p, m_numElements * sizeof(T));
        }
        else
        {
            for (ULONG i = 0; i < m_numElements; i++)
                new (wistd::addressof(p[i])) T(wistd::move(_p[i]));
        }

        if (_p)
        {
            for (ULONG i = 0; i < m_numElements; i++)
                _p[i].~T();

            ExFreePoolWithTag(_p, 'rrAK');
        }

        m_bufferSize = static_cast<ULONG>(count);
        _p = p;

        return true;
    }

    PAGED bool resize(size_t count)
    {
        if (!reserve(count))
            return false;

        if constexpr(wistd::is_trivially_default_constructible_v<T>)
        {
            if (count > m_numElements)
            {
                memset(wistd::addressof(_p[m_numElements]), 0, (count - m_numElements) * sizeof(T));
            }
        }
        else
        {
            for (size_t i = m_numElements; i < count; i++)
            {
                new(wistd::addressof(_p[i])) T();
            }
        }

        if constexpr(!wistd::is_trivially_destructible_v<T>)
        {
            for (size_t i = count; i < m_numElements; i++)
            {
                _p[i].~T();
            }
        }

        m_numElements = static_cast<ULONG>(count);
        return true;
    }

    PAGED void clear(void)
    {
        (void)resize(0);
    }

    PAGED bool append(T const &t)
    {
        if (!grow((size_t)m_numElements+1))
            return false;

        new(wistd::addressof(_p[m_numElements])) T(t);
        ++m_numElements;
        return true;
    }

    PAGED bool push_back(T const &t)
    {
        return append(t);
    }

    PAGED bool append(T &&t)
    {
        if (!grow((size_t)m_numElements+1))
            return false;

        new(wistd::addressof(_p[m_numElements])) T(wistd::move(t));
        ++m_numElements;
        return true;
    }

    PAGED bool push_back(T &&t)
    {
        return append(t);
    }

    PAGED void pop_back()
    {
        (void)resize(count() - 1);
    }

    PAGED bool insertAt(size_t index, T &t)
    {
        if (index > m_numElements)
            return false;

        if (!grow((size_t)m_numElements+1))
            return false;

        if (index < m_numElements)
            moveElements((ULONG)index, (ULONG)(index+1), (ULONG)(m_numElements - index));

        new(wistd::addressof(_p[index])) T(t);
        ++m_numElements;
        return true;
    }

    PAGED bool insertAt(size_t index, T &&t)
    {
        if (index > m_numElements)
            return false;

        if (!grow((size_t)m_numElements+1))
            return false;

        if (index < m_numElements)
            moveElements((ULONG)index, (ULONG)(index+1), (ULONG)(m_numElements - index));

        new(wistd::addressof(_p[index])) T(wistd::move(t));
        ++m_numElements;
        return true;
    }

    PAGED bool insertSorted(T &t, bool (*lessThanPredicate)(T const&, T const&))
    {
        for (size_t i = 0; i < m_numElements; i++)
        {
            if (!lessThanPredicate(_p[i], t))
            {
                return insertAt(i, t);
            }
        }

        return append(t);
    }

    PAGED bool insertSorted(T &&t, bool (*lessThanPredicate)(T const&, T const&))
    {
        for (size_t i = 0; i < m_numElements; i++)
        {
            if (!lessThanPredicate(_p[i], t))
            {
                return insertAt(i, wistd::move(t));
            }
        }

        return append(wistd::move(t));
    }

    PAGED bool insertSortedUnique(T &t, bool (*lessThanPredicate)(T const&, T const&))
    {
        for (size_t i = 0; i < m_numElements; i++)
        {
            if (!lessThanPredicate(_p[i], t))
            {
                if (lessThanPredicate(t, _p[i]))
                    return insertAt(i, t);
                else
                    return true;
            }
        }

        return append(t);
    }

    PAGED bool insertSortedUnique(T &&t, bool (*lessThanPredicate)(T const&, T const&))
    {
        for (size_t i = 0; i < m_numElements; i++)
        {
            if (!lessThanPredicate(_p[i], t))
            {
                if (lessThanPredicate(t, _p[i]))
                    return insertAt(i, wistd::move(t));
                else
                    return true;
            }
        }

        return append(wistd::move(t));
    }

    PAGED void eraseAt(size_t index)
    {
        if (index >= m_numElements)
            RtlFailFast(FAST_FAIL_INVALID_ARG);

        _p[index].~T();
        moveElements((ULONG)(index+1), (ULONG)index, (ULONG)(m_numElements - index - 1));
        --m_numElements;
    }

    NONPAGED T &operator[](size_t index)
    {
        if (index >= m_numElements)
            RtlFailFast(FAST_FAIL_INVALID_ARG);

        return _p[index];
    }

    NONPAGED T const &operator[](size_t index) const
    {
        if (index >= m_numElements)
            RtlFailFast(FAST_FAIL_INVALID_ARG);

        return _p[index];
    }

    NONPAGED T * data() const
    {
        return _p;
    }

    PAGED iterator begin()
    {
        return { this, 0 };
    }

    PAGED const_iterator begin() const
    {
        return { this, 0 };
    }

    PAGED iterator end()
    {
        return { this, m_numElements };
    }

    PAGED const_iterator end() const
    {
        return { this, m_numElements };
    }

private:

    NONPAGED void reset()
    {
        if (_p)
        {
            if constexpr(!wistd::is_trivially_destructible_v<T>)
            {
                for (auto i = m_numElements; i > 0; i--)
                {
                    _p[i-1].~T();
                }
            }

            ExFreePoolWithTag(_p, 'rrAK');
            _p = nullptr;
            m_numElements = 0;
            m_bufferSize = 0;
        }
    }

    PAGED void moveElements(ULONG from, ULONG to, ULONG number)
    {
        if (from == to || number == 0)
        {
            NOTHING;
        }
        else if constexpr(__is_trivially_copyable(T))
        {
            memmove(_p + to, _p + from, number * sizeof(T));
        }
        else if (from < to)
        {
            CXPLAT_FRE_ASSERT(m_numElements == from + number);

            ULONG delta = to - from;
            ULONG i;

            for (i = to + number; i - 1 >= m_numElements; i--)
            {
                new (wistd::addressof(_p[i - 1])) T(wistd::move(_p[i - delta - 1]));
            }

            for (NOTHING; i > to; i--)
            {
                _p[i - 1].~T();
                new (wistd::addressof(_p[i - 1])) T(wistd::move(_p[i - delta - 1]));
            }

            for (NOTHING; i > from; i--)
            {
                _p[i - 1].~T();
            }
        }
        else
        {
            CXPLAT_FRE_ASSERT(m_numElements == from + number);

            ULONG delta = from - to;
            ULONG i;

            for (i = to; i < from; i++)
            {
                new (wistd::addressof(_p[i])) T(wistd::move(_p[i + delta]));
            }

            for (NOTHING; i < to + number; i++)
            {
                _p[i].~T();
                new (wistd::addressof(_p[i])) T(wistd::move(_p[i + delta]));
            }

            for (NOTHING; i < from + number; i++)
            {
                _p[i].~T();
            }
        }
    }

    PAGED bool grow(size_t count)
    {
        if (m_bufferSize >= count)
            return true;

        if (count < 4)
            count = 4;

        size_t exponentialGrowth = (size_t)m_bufferSize + m_bufferSize / 2;
        if (count < exponentialGrowth)
            count = exponentialGrowth;

        return reserve(count);
    }

    friend class ::triageClass;
    friend VOID ::initGlobalTriageBlock();

    ULONG m_bufferSize = 0;
    ULONG m_numElements = 0;
    T *_p = nullptr;
};

}