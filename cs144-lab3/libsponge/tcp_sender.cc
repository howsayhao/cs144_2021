#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) 
    , _window_size(1)
    , _ackno(0)
    , _left_timeout(retx_timeout)
    , _retransmit_count(0)
    , _timer_start(false)
    , _syn_flag(false)
    , _fin_flag(false)
    , _retransmit_timeout(retx_timeout)
{


}

uint64_t TCPSender::bytes_in_flight() const { 
    uint64_t flight_bytes = 0;
    auto it = _outstands.begin();
    for(; it != _outstands.end(); it++)
        flight_bytes += it->second.length_in_sequence_space();
    return flight_bytes; 
}

void TCPSender::fill_window() {
    if (!_syn_flag) {
        TCPSegment seg;
        seg.header().syn = true;
        send_segments(seg);
        _syn_flag = true;
        return;
    }
    if( _fin_flag )
        return;
    
    size_t win_size = _window_size > 0? _window_size : 1;
    size_t remain;
    while((remain = win_size - (_next_seqno - _ackno)) != 0) {
        TCPSegment seg;
        size_t size = min(TCPConfig::MAX_PAYLOAD_SIZE, remain);
        string str = _stream.read(size);
        seg.payload() = Buffer(std::move(str));
        if (_stream.eof() && seg.length_in_sequence_space() < size && !_fin_flag) {
            seg.header().fin = true;
            _fin_flag = true;
            send_segments(seg);
            return;
        }
        if (seg.length_in_sequence_space() == 0) {
            return;
        }
        send_segments(seg);
    }

}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
//! \returns `false` if the ackno appears invalid (acknowledges something the TCPSender hasn't sent yet)
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _ackno);
    _window_size = window_size;
    if(abs_ackno > _next_seqno)
        return false;
    if(abs_ackno <= _ackno)
        return true;
    _ackno = abs_ackno;

    if(!_outstands.empty())
        while((_ackno  >= (unwrap(_outstands.begin()->second.header().seqno, _isn, _ackno) \
                        + _outstands.begin()->second.length_in_sequence_space()))\
            && (bytes_in_flight() > 0) && !_outstands.empty())
        {
            _outstands.erase(_outstands.begin()->first);
            _retransmit_count = 0;
            _retransmit_timeout = _initial_retransmission_timeout;
            _left_timeout = _retransmit_timeout;
            if(_outstands.empty())
                break;
        }

    if(! bytes_in_flight()) {
        _timer_start = false;
    } else {
        _timer_start = true;
    }

    fill_window();
    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    if(_outstands.empty())
        _timer_start = false;
    if(_timer_start){
        _left_timeout = (_left_timeout<ms_since_last_tick)?0:_left_timeout-ms_since_last_tick;
        if(_left_timeout == 0){
            _segments_out.push(_outstands.begin()->second);
            if(_window_size)
            {
                _retransmit_timeout *= 2;
                _retransmit_count ++;
            }

            // reset
            _left_timeout = _retransmit_timeout;
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retransmit_count; }

void TCPSender::send_empty_segment() {
    TCPSegment empty_segment;
    empty_segment.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(empty_segment);
}


void TCPSender::send_segments(TCPSegment &seg) {
    seg.header().seqno = wrap(_next_seqno, _isn);
    _outstands[_next_seqno] = seg;
    _next_seqno += seg.length_in_sequence_space();
    _segments_out.push(seg);
    if(! _timer_start)
        _timer_start = true;
    _left_timeout = _retransmit_timeout;
}
