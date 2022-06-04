#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader header = seg.header();
    // 处理syn报文的
    if (!_syn_set) {
        if (!header.syn) 
            return false;
        _isn = header.seqno;
        _ackno = _isn;
        _syn_set = true;
    } else if(header.syn) {
        return false;
    }

    if (_fin_set && header.fin)
        return false;

    auto seg_seqno_start = unwrap(header.seqno, _isn, _checkpoint); // 到达报文的起始
    auto seg_seqno_end = seg_seqno_start + (seg.length_in_sequence_space() ? seg.length_in_sequence_space() - 1 : 0); // 到达报文的末端
    auto seqno_start = unwrap(_ackno, _isn, _checkpoint); // 窗口允许的到达报文起始
    auto seqno_end = seqno_start + (window_size() ? window_size() - 1 : 0); // 窗口允许的到达报文末端
    auto payload_end = seg_seqno_end; // 在窗口列上的到达报文的实际末端

    if (header.syn && header.fin) {
        payload_end = payload_end > 2 ? payload_end - 2 : 0;
    } else if (header.syn || header.fin) {
        payload_end = payload_end > 1 ? payload_end - 1 : 0;
    }

    bool fall_into_window =    (seg_seqno_start >= seqno_start && seg_seqno_start <= seqno_end) \
                            || (payload_end >= seqno_start && seg_seqno_end <= seqno_end);  // 报文至少有最开始有一段在窗口内,或者报文至少有最后一段在窗口内
    if (fall_into_window)
        _reassembler.push_substring(seg.payload().copy(), seg_seqno_start - 1, header.fin);
    
    if (!_fin_set && header.fin) {
        _fin_set = true; // 接受结束
        if (header.syn && seg.length_in_sequence_space() == 2) // 如果这次连接只有这一段报文，那么直接接收结束
            _reassembler.stream_out().end_input();
    }

    _checkpoint = _reassembler.stream_out().bytes_written();
    if (payload_end <= _checkpoint) {
        _ackno = wrap(_reassembler.stream_out().bytes_written() +
                          (_fin_set && (_reassembler.unassembled_bytes() == 0) ? 1 : 0) + 1,
                      _isn);
    }

    if (fall_into_window || header.fin || header.syn) {
        return true;
    }
    return false;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_syn_set) {
        return {};
    } else {
        return _ackno;
    }
}

size_t TCPReceiver::window_size() const {
    // the lab documentation gives a wrong guide here
    return _capacity - _reassembler.stream_out().buffer_size();
}
