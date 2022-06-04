#include "tcp_connection.hh"

#include <iostream>
#include <fstream>

void print_log(size_t warn);

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active) // not active, 处于CLOSED状态
        return;
    _time_since_last_segment_received = 0;
    bool send_empty = false;
    
    // in_listen
    if (in_listen()) {
        // 三次握手之二, -> in_syn_recv
        // 那么已经准备好接受数据了，发送ack
        // 同时还要发送syn来准备发送数据
        if (seg.header().syn) { 
            _receiver.segment_received(seg); /* 这样才能切换出listen的状态
                                                否则永远不会回应ack     */
            connect();
            return;
        } 
    }

    // in_syn_sent, 这个只有连接建立方才会经历
    if (in_syn_sent()) {
        if (seg.header().ack && seg.payload().size() > 0) // 只接受连接回应ack，connect不应该有数据
            return;
        if (seg.header().ack && seg.header().syn) {  // 三次握手之三，连接建立，可以开始发送数据报文
                                                     // 但因为这里默认第三次握手还是不会附带数据报文，所以这里直接发空报文了
                send_empty = true;
            }
        else if (not seg.header().ack && seg.header().syn) { // 说明两方都同时都发起了建立连接申请
                                                             // 那我们就退让，回应以ack/syn
                                                             // 由于syn之前已经发过了，现在发ack, 这样的好处就是状态可以到in_syn_rev，不会跳出原定的状态机
                send_empty = true;
            }
        else {} // 主要是留给rst报文的
    }

    // 处理rst重置报文
    if (seg.header().rst) {// 重置报文
        if (in_syn_sent() && not seg.header().ack) { // 这种是异常情况，不管，其实我也没碰到过，抄的代码，以防万一
            return;
        }
        unclean_shutdown(false);
        return;
    }

    // in_syn_recv, 这个只有被连接建立方才会经历
    if (in_syn_recv()) {
        if (seg.header().ack) {  // 连接建立，理论上这个时候才算被建立连接方确定自己可以发送而进入建立状态了
                                 // 这个时候，这次的报文已经带有真实的数据了（虽然测试没有）
                                 // 不需要发送空报文
                                 // 等有数据或向发送时再发送，然后附带自己的ack即可
                // not_send_till_data = true; // 不需要，按题意，对ack的发送并不吝啬
                                              // 只要来报有长度，一定发一个ack
                                              // 只要是三次握手，和四次握手，就经常发ack
                                              // 每次自己要发数据时，除了刚开始也都要带ack
                                              // 所以不用加限制，整体来看这样反而更便于tcp通信
            }
        else return; // rst报文情况前面已经处理
                     // 故直接返回
    }

    // 官方文档给的“still-alive”报文处理
    if (_receiver.ackno().has_value() and (seg.length_in_sequence_space() == 0) and seg.header().seqno == _receiver.ackno().value() - 1) {
        send_empty = true;
    }

    // 发送端 接受 ack
    if (seg.header().ack) {
        if (!_sender.ack_received(seg.header().ackno, seg.header().win)) {// 判断ack有效性，并更新
            send_empty = true; // 如果ack无效，实际上window没有更新新的发送报文
                               // 需要自己发送一个空报文， 来承载ack
                               // 也需要来告知对等方自己目前的情况
        }        
    }

    // 接收端 接受 报文数据
    bool recv_data = _receiver.segment_received(seg); // 接受segment报文数据
    if (not recv_data) {    // 报文没有有效数据, 注意，不是没有数据，是没有有效数据
                            // 具体为，syn/fin的错误发送，报文不在窗口内
                            // 算是异常情况，所以需要发送ack
                            // 但问题是，只带ack的空报文也满足这个条件， 我之前担心会像滚雪球一样越滚越大
                            // 毕竟这个ack发送太奢侈了
                            // 所以我在后面改了下逻辑，如果_sender的segment_out有报文待发
                            // 就不要发空报文了，这样可以避免这种情况，避免拥堵（或许吧，毕竟ack空报文也不大）
        send_empty = true; 
    }
    if (seg.length_in_sequence_space() > 0) { // 文档说对任何有序列号的，都要返回一个ack
                                              // 我觉得是如果报文有长度，那就说明不是空报文，就有序列号了
                                              // 不过我后来发现测试里无长度的ack报文有的也有序列号，，还好前面的都基本考虑了，虽有gap，但测试能过
                                              // 我觉得也ok， ack本来就不指望每次都能回应，（而且发的也确实很多了，无伤大雅，and 连接和断开两个阶段又完全不受这个影响
        send_empty = true;
    }
    if (clean_shutdown()) { return; }  // 如果可以完全关闭，就不要到后面再发送报文了
    
    // 四次握手回应判断， 如果只是本地请求终止的回应则无需发送ack报文告知收到
    if (_wait_for_fin_ack && seg.header().ack && seg.length_in_sequence_space() == 0) {  // 因为这种回应报文不带数据，也不带Fin, 因而长度为0
        _wait_for_fin_ack = false;
        push_segments_out();
        return;
    } 
    
    // 四次握手阶段需要发送ack回应的两种情况
    if (_wait_for_fin_ack || seg.header().fin)
        send_empty = true;

    // 发送空报文ack
    if (send_empty && _sender.segments_out().empty()) {  // 改了下逻辑
        if(_receiver.ackno().has_value() && not clean_shutdown()) {
                _sender.send_empty_segment();
        }
    }
        
    push_segments_out();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    if(data.length() == 0)
        return 0;

    size_t ret = _sender.stream_in().write(data);// 写入缓存
    _sender.fill_window();
    push_segments_out();// 封装成帧
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active)
        return;

    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);// 同步时间
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        unclean_shutdown(true);
    }

    push_segments_out();
    clean_shutdown();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _wait_for_fin_ack = true;
    // _ackno_of_fin = _sender.next_seqno();
    push_segments_out();
}

//! \brief 发送syn 或 syn/ack, 分别只会在三次握手之一和三次握手之二的时候会被用到， 建立连接
void TCPConnection::connect() { // 连接
    if(not _active) { // 作为连接建立方，发送请求时即激活该连接
                      // 作为被连接建立方，三次握手之二时激活该连接
        _active = true;
    }
    push_segments_out(true); // 发送连接，通过fill_window
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            // Your code here: need to send a RST segment to the peer
            unclean_shutdown(true);// 关于处理 非正常关闭的函数   例如 设置 rst
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

//! \brief 非正常关闭, 如果设置了send_rst， 则还要发送rst报文关闭对等方
void TCPConnection::unclean_shutdown(bool send_rst) {
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
    if (send_rst) { // send a rst segment
        _need_send_rst = true;
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
        push_segments_out();
    }
}

//! \brief 正常关闭，四次握手
bool TCPConnection::clean_shutdown() {
    // print_log(43);
    if (_receiver.stream_out().input_ended() && (not _sender.stream_in().eof())) {// 工作未完全结束  prereq 1-2
        _linger_after_streams_finish = false;
    }
    if (_sender.stream_in().eof() && _sender.bytes_in_flight() == 0 && _receiver.stream_out().input_ended()) {
        if ((not _linger_after_streams_finish) || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {// prereq1-3 or timeout
            _active = false;
            _time_since_last_segment_received = 0;
        }
    }
    return not _active;
}

//! \brief 把所有sender的_segment_out都pop到connection的segment_out，加一些header()修改
bool TCPConnection::push_segments_out(bool send_syn) {
    _sender.fill_window( send_syn || in_syn_recv());// send_syn  或者  in_syn_recv要回应
                                                    // 此时如果还没发送过syn，会发送syn报文
                                                    // 或是作为三次握手之一的连接建立请求
                                                    // 或是作为三次握手之二的syn/ack请求
    TCPSegment seg;
    while (not _sender.segments_out().empty()) { // 在三次握手之二后，给所有发送报文添上ack, and info
                                                 // 当然在三次握手之一时，不需要添上ack
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) { // 如果作为接收方，已经收到过syn，那么每次都要ack回应
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        if (_need_send_rst) {
            _need_send_rst = false;
            seg.header().rst = true;
        }
        _segments_out.push(seg);
    }
    return true;
}

// 自初始化这个连接以来，还未收到过ack，也还未发送过
//! \brief 没有发送过报文(syn,因为只有syn后才可能发其他报文)，也没有被ack过
bool TCPConnection :: in_listen(){ 
    return !_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0;
}

//! \brief 这个状态的意思是，收到过syn了，且也发了一个syn作为回应，未被ack，作为被建立连接方三次握手之二的状态
bool TCPConnection :: in_syn_recv(){
    return _receiver.ackno().has_value() && !_receiver.stream_out().input_ended() \
            && _sender.bytes_in_flight() == 1 && _sender.outstands().begin()->second.header().syn;
}

// 发送过且只发送过一个syn报文，且这个报文还未被ack
//! \brief 作为连接建立方，发送过syn但还未被ack
bool TCPConnection :: in_syn_sent(){
    return _sender.next_seqno_absolute() == 1 && _sender.bytes_in_flight() == 1 \
           && _sender.outstands().begin()->second.header().syn;
}

//! \brief 打印日志，本想写到文件log里，发现直接cerr更方便
void print_log(size_t warn) {
    ofstream log;
    log.open("log", ios::app);
    cerr << warn << endl;
    log.close();
}

