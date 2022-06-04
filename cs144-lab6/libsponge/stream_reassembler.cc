#include "stream_reassembler.hh"
#include "iostream"
#include "string"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;


StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity),_capacity(capacity),_next_index(0),_windows(capacity-1),_eof(false){
    _buffer.clear();
}


//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) 
{
    size_t length = data.length();
    _windows = _capacity - _output.buffer_size() + _next_index - 1;
    size_t max_length = _windows - index + 1;
    if(length > max_length){
        length = max_length;
    }        

    size_t i = index;
    if(index < _next_index)
        i = _next_index;
    for(; i < index + length; i++){
        if(_buffer.find(i) != _buffer.end()) // 靠，我都看不懂以前写的代码了 ， 意思应该是如果这个非空那就不要赋值了。。
            continue;
        _buffer[i] = data[i-index];
    }

    if(data.length() != 0) // for the first time, a null begin()->first is 0, where the _next_index is 0 as well; if data not empty, this case can ignore
        while(_buffer.begin()->first == _next_index){
            _next_index ++;
            _output.write(_buffer.begin()->second);
            _buffer.erase(_buffer.begin()->first);
        }

    if(eof && length == data.length())
        _eof = true;
    if(_eof && empty()){
        _output.end_input();
        _buffer.clear();
        _next_index = 0;
    }

}

size_t StreamReassembler::unassembled_bytes() const { return _buffer.size(); }

bool StreamReassembler::empty() const { return _buffer.empty(); }

// size_t StreamReassembler::_first_unassembled() const { return _output.bytes_written(); }