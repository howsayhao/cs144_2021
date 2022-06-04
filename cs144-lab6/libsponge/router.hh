#ifndef SPONGE_LIBSPONGE_ROUTER_HH
#define SPONGE_LIBSPONGE_ROUTER_HH

#include "network_interface.hh"

#include <optional>
#include <queue>
#include <map>

//! \brief A wrapper for NetworkInterface that makes the host-side
//! interface asynchronous: instead of returning received datagrams
//! immediately (from the `recv_frame` method), it stores them for
//! later retrieval. Otherwise, behaves identically to the underlying
//! implementation of NetworkInterface.
class AsyncNetworkInterface : public NetworkInterface {
    std::queue<InternetDatagram> _datagrams_out{};

  public:
    using NetworkInterface::NetworkInterface;

    //! Construct from a NetworkInterface
    AsyncNetworkInterface(NetworkInterface &&interface) : NetworkInterface(interface) {}

    //! \brief Receives and Ethernet frame and responds appropriately.

    //! - If type is IPv4, pushes to the `datagrams_out` queue for later retrieval by the owner.
    //! - If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
    //! - If type is ARP reply, learn a mapping from the "target" fields.
    //!
    //! \param[in] frame the incoming Ethernet frame
    void recv_frame(const EthernetFrame &frame) {
        auto optional_dgram = NetworkInterface::recv_frame(frame);
        // 如果返回的是ipv4数据报，接受了，存入_datagrams_out作下一步的forwarding
        // 每个interface都有一个自己的_datagrams_out，即数据报缓存，以供router使用
        // 相当于更改了本来的interface功能，将本来接收的ipv4再转发一份到路由路径上
        if (optional_dgram.has_value()) {
            _datagrams_out.push(std::move(optional_dgram.value()));
        }
    };

    //! Access queue of Internet datagrams that have been received
    std::queue<InternetDatagram> &datagrams_out() { return _datagrams_out; }
};




//! \brief A router that has multiple network interfaces and
//! performs longest-prefix-match routing between them.
class Router {
    //! The router's collection of network interfaces
    // 一个router管理着多个interface
    std::vector<AsyncNetworkInterface> _interfaces{};


    //! Send a single datagram from the appropriate outbound interface to the next hop,
    //! as specified by the route with the longest prefix_length that matches the
    //! datagram's destination address.
    void route_one_datagram(InternetDatagram &dgram);

    struct _select_route
    {
        uint32_t _route_prefix = 0;
        uint8_t _prefix_length = 0;
        std::optional<Address> _next_hop = {};
        size_t _interface_num = 0;  /*  interface和next_hop是完全不一样的东西
                                    router有多个interface,这些interface或许都有自己的ip地址
                                    但对路由选择来说，这些interface的ip在转发时主要是作为发起方，而对向谁发并不能够决定
                                    next_hop也不是这些interface的ip地址，而是我要发往的interface的ip    */
    };
    // 用一下vector,不要总是用map
    std::vector<_select_route> _route_table{};


  public:
    //! Add an interface to the router
    //! \param[in] interface an already-constructed network interface
    //! \returns The index of the interface after it has been added to the router
    size_t add_interface(AsyncNetworkInterface &&interface) {
        _interfaces.push_back(std::move(interface));
        return _interfaces.size() - 1;
    }

    //! Access an interface by index
    AsyncNetworkInterface &interface(const size_t N) { return _interfaces.at(N); }

    //! Add a route (a forwarding rule)
    void add_route(const uint32_t route_prefix,
                   const uint8_t prefix_length,
                   const std::optional<Address> next_hop,
                   const size_t interface_num);

    //! Route packets between the interfaces
    void route();
};

#endif  // SPONGE_LIBSPONGE_ROUTER_HH
