#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer
#include <iostream>
// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t raw_n = static_cast<uint32_t>((n << 32) >> 32) + isn.raw_value();

    return WrappingInt32(raw_n);
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) { // copy from zhihu, really great, at least 2 great points!
    WrappingInt32 wrap_checkpoint = wrap(checkpoint, isn);
    uint32_t diff = n.raw_value() - wrap_checkpoint.raw_value();
    if (static_cast<int64_t>(static_cast<int32_t>(diff) + checkpoint) < 0) {
        return checkpoint + diff;
    }
    return checkpoint + static_cast<int32_t>(diff);
}
