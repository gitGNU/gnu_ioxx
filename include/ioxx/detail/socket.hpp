/*
 * Copyright (c) 2008 Peter Simons <simons@cryp.to>
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from the
 * use of this software.
 *
 * Copying and distribution of this file, with or without modification, are
 * permitted in any medium without royalty provided the copyright notice and
 * this notice are preserved.
 */

#ifndef IOXX_DETAIL_SOCKET_HPP_INCLUDED_2008_04_20
#define IOXX_DETAIL_SOCKET_HPP_INCLUDED_2008_04_20

#include <ioxx/detail/config.hpp>
#include <ioxx/detail/error.hpp>
#include <boost/noncopyable.hpp>
#include <iosfwd>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

namespace ioxx { namespace detail
{
  class socket : private boost::noncopyable
  {
  public:
    typedef native_socket_t native_t;

    enum service_type_tag { stream_service = SOCK_STREAM, datagram_service = SOCK_DGRAM };

    class address
    {
    public:
      typedef char host_name[NI_MAXHOST];
      typedef char service_name[NI_MAXSERV];

      address() : _len(0) { }
      address(sockaddr const & addr, socklen_t len) : _addr(addr), _len(len) { }

      address(char const * host, char const * service, service_type_tag type = stream_service)
      {
        addrinfo const hint = { AI_NUMERICHOST, 0, type, 0, 0u, NULL, NULL, NULL };
        addrinfo * addr;
        int const rc( getaddrinfo(host, service, &hint, &addr) );
        if (rc != 0) throw std::runtime_error( gai_strerror(rc) );
        BOOST_ASSERT(addr);
        BOOST_ASSERT(addr->ai_socktype == type);        // we got the type we requested
        BOOST_ASSERT(!addr->ai_next);                   // only one result
        BOOST_ASSERT(!addr->ai_addrlen <= sizeof(sockaddr));
        _addr = *addr->ai_addr;
        _len  = addr->ai_addrlen;
        freeaddrinfo(addr);
      }

      void show(host_name & host, service_name & service) const
      {
        int const rc( getnameinfo(&_addr, _len, host, sizeof(host), service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV) );
        if (rc != 0) throw std::runtime_error(gai_strerror(rc));
      }

      std::string show() const
      {
        host_name host;
        service_name service;
        show(host, service);
        return std::string(host) + ':' + service;
      }

      sockaddr &         as_sockaddr()        { return _addr; }
      sockaddr const &   as_sockaddr()  const { return _addr; }

      socklen_t &        as_socklen_t()       { return _len; }
      socklen_t const &  as_socklen_t() const { return _len; }

      friend std::ostream & operator<< (std::ostream & os, address const & addr) { return os << addr.show(); }

    protected:
      sockaddr  _addr;
      socklen_t _len;
    };

    class endpoint : public address
    {
    public:
      endpoint() { }

      endpoint(char const * host, char const * service, service_type_tag type = stream_service)
      {
        addrinfo const hint = { AI_NUMERICHOST, 0, type, 0, 0u, NULL, NULL, NULL };
        addrinfo * addr;
        int const rc( getaddrinfo(host, service, &hint, &addr) );
        if (rc != 0) throw std::runtime_error( gai_strerror(rc) );
        BOOST_ASSERT(addr);
        BOOST_ASSERT(addr->ai_socktype == type);        // we got the type we requested
        BOOST_ASSERT(!addr->ai_next);                   // only one result
        BOOST_ASSERT(!addr->ai_addrlen <= sizeof(sockaddr));
        _addr     = *addr->ai_addr;
        _len      = addr->ai_addrlen;
        _domain   = addr->ai_family;
        _socktype = addr->ai_socktype;
        _protocol = addr->ai_protocol;
        freeaddrinfo(addr);
      }

      native_socket_t create() const
      {
        return throw_errno_if_minus1("socket(2)", boost::bind(boost::type<int>(), &::socket, _domain, _socktype, _protocol));
      }

    protected:
      int       _domain;
      int       _socktype;
      int       _protocol;
    };

    enum ownership_type_tag { weak, take_ownership };

    explicit socket(native_socket_t sock, ownership_type_tag owner = take_ownership) : _sock(sock)
    {
      LOGXX_GET_TARGET(LOGXX_SCOPE_NAME, "ioxx.socket." + show(_sock));
      if (_sock < 0) throw std::invalid_argument("cannot construct an invalid ioxx::socket");
      close_on_destruction(owner == take_ownership);
    }

    ~socket()
    {
      IOXX_TRACE_MSG((_close_on_destruction ? "close and " : "") << "destruct ");
      if (_close_on_destruction)
        throw_errno_if_minus1("close(2)", boost::bind(boost::type<int>(), &::close, _sock));
    }

    void close_on_destruction(bool enable = true)
    {
      IOXX_TRACE_MSG((enable ? "enable" : "disable") << " close-on-destruction semantics");
      _close_on_destruction = enable;
    }

    bool close_on_destruction() const
    {
      return _close_on_destruction;
    }

    void set_nonblocking(bool enable = true)
    {
      IOXX_TRACE_MSG((enable ? "enable" : "disable") << " nonblocking mode");
      int const rc( throw_errno_if_minus1("cannot obtain socket flags", boost::bind<int>(&::fcntl, _sock, F_GETFL, 0)) );
      int const flags( enable ? rc | O_NONBLOCK : rc & ~O_NONBLOCK );
      if (rc != flags)
        throw_errno_if_minus1("cannot set socket flags", boost::bind<int>(&::fcntl, _sock, F_SETFL, flags));
    }

    void set_linger_timeout(unsigned short second_timeout)
    {
      linger ling;
      ling.l_onoff  = second_timeout > 0 ? 1 : 0;
      ling.l_linger = static_cast<int>(second_timeout);
      throw_errno_if_minus1("set socket lingering", boost::bind(boost::type<int>(), &::setsockopt, _sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(linger)));
    }

    void reuse_bind_address(bool enable = true)
    {
      int true_flag = enable ? 1 : 0;
      throw_errno_if_minus1("bind with SO_REUSEADDR", boost::bind(boost::type<int>(), &::setsockopt, _sock, SOL_SOCKET, SO_REUSEADDR, &true_flag, sizeof(int)));
    }

    void bind(address const & addr)
    {
      throw_errno_if_minus1("bind(2)", boost::bind(boost::type<int>(), &::bind, _sock, &addr.as_sockaddr(), addr.as_socklen_t()));
    }

    void listen(unsigned short backlog)
    {
      throw_errno_if_minus1("listen(2)", boost::bind(boost::type<int>(), &::listen, _sock, static_cast<int>(backlog)));
    }

    bool accept(native_socket_t & s, address & addr)
    {
      addr.as_socklen_t() = sizeof(sockaddr);
      s = detail::throw_errno_if( detail::not_ewould_block(), "accept(2)"
                                , boost::bind(boost::type<int>(), &::accept, as_native_socket_t(), &addr.as_sockaddr(), &addr.as_socklen_t())
                                );
      return s >= 0;
    }

    char * read(char * begin, char const * end)
    {
      IOXX_TRACE_MSG("read up to " << end - begin << " bytes");
      BOOST_ASSERT(begin < end);
      ssize_t const rc( throw_errno_if( not_ewould_block()
                                      , "read(2)"
                                      , boost::bind(boost::type<ssize_t>(), & ::read, _sock, begin, static_cast<size_t>(end - begin))
                                      ));
      IOXX_TRACE_MSG("read(2) received " << rc << " bytes");
      switch (rc)
      {
        case -1:  BOOST_ASSERT(errno == EWOULDBLOCK || errno == EAGAIN); return begin;
        case 0:   return 0;
        default:  return begin + rc;
      };
    }

    char const * write(char const * begin, char const * end)
    {
      IOXX_TRACE_MSG("write up to " << end - begin << " bytes");
      BOOST_ASSERT(begin < end);
      ssize_t const rc( throw_errno_if( not_ewould_block()
                                      , "write(2)"
                                      , boost::bind(boost::type<ssize_t>(), & ::write, _sock, begin, static_cast<size_t>(end - begin))
                                      ));
      IOXX_TRACE_MSG("write(2) sent " << rc << " bytes");
      switch (rc)
      {
        case -1:  BOOST_ASSERT(errno == EWOULDBLOCK || errno == EAGAIN); return begin;
        case 0:   return 0;
        default:  return begin + rc;
      };
    }

    ssize_t readv(iovec * begin, iovec const * end)
    {
      BOOST_ASSERT(begin < end);
      ssize_t const rc( throw_errno_if( not_ewould_block()
                                      , "readv(2)"
                                      , boost::bind(boost::type<ssize_t>(), & ::readv, _sock, begin, static_cast<int>(end - begin))
                                      ));
      IOXX_TRACE_MSG("readv(2) received " << rc << " bytes");
      return rc;
    }

    ssize_t writev(iovec const * begin, iovec const * end)
    {
      BOOST_ASSERT(begin < end);
      ssize_t const rc( throw_errno_if( not_ewould_block()
                                      , "writev(2)"
                                      , boost::bind(boost::type<ssize_t>(), & ::writev, _sock, begin, static_cast<int>(end - begin))
                                      ));
      IOXX_TRACE_MSG("writev(2) wrote " << rc << " bytes");
      return rc;
    }

    ssize_t recv_from(iovec * begin, iovec const * end, address & from)
    {
      BOOST_ASSERT(begin < end);
      msghdr msg =
        { &from.as_sockaddr()
        , static_cast<socklen_t>(sizeof(sockaddr))
        , begin
        , static_cast<size_t>(end - begin)
        , static_cast<void *>(0)                    // control data
        , static_cast<socklen_t>(0)                 // control data size
        , static_cast<int>(0)                       // flags: set on return
        };
      ssize_t const rc( throw_errno_if( not_ewould_block()
                                      , "recvmsg(2)"
                                      , boost::bind(boost::type<ssize_t>(), & ::recvmsg, _sock, &msg, static_cast<int>(MSG_DONTWAIT))
                                      ));
      IOXX_TRACE_MSG("recvmsg(2) received " << rc << " bytes");
      from.as_socklen_t() = msg.msg_namelen;
      return rc;
    }

    ssize_t send_to(iovec const * begin, iovec const * end, address const & to)
    {
      BOOST_ASSERT(begin < end);
      msghdr msg =
        { const_cast<sockaddr *>(&to.as_sockaddr())
        , to.as_socklen_t()
        , const_cast<iovec *>(begin)
        , static_cast<size_t>(end - begin)
        , static_cast<void *>(0)                    // control data
        , static_cast<socklen_t>(0)                 // control data size
        , static_cast<int>(0)                       // flags: set on return
        };
      return throw_errno_if(not_ewould_block(), "sendmsg(2)", boost::bind(boost::type<ssize_t>(), & ::sendmsg, _sock, &msg, static_cast<int>(MSG_DONTWAIT)));
    }

    address local_address() const
    {
      address addr;
      addr.as_socklen_t() = sizeof(sockaddr);
      throw_errno_if_minus1("getsockname(2)", boost::bind(boost::type<int>(), &::getsockname, _sock, &addr.as_sockaddr(), &addr.as_socklen_t()));
      return addr;
    }

    address peer_address() const
    {
      address addr;
      addr.as_socklen_t() = sizeof(sockaddr);
      throw_errno_if_minus1("getpeername(2)", boost::bind(boost::type<int>(), &::getpeername, _sock, &addr.as_sockaddr(), &addr.as_socklen_t()));
      return addr;
    }

    native_socket_t as_native_socket_t() const { return _sock; }

    friend bool operator<  (socket const & lhs, socket const & rhs) { return lhs._sock <  rhs._sock; }
    friend bool operator<= (socket const & lhs, socket const & rhs) { return lhs._sock <= rhs._sock; }
    friend bool operator== (socket const & lhs, socket const & rhs) { return lhs._sock == rhs._sock; }
    friend bool operator!= (socket const & lhs, socket const & rhs) { return lhs._sock != rhs._sock; }
    friend bool operator>= (socket const & lhs, socket const & rhs) { return lhs._sock >= rhs._sock; }
    friend bool operator>  (socket const & lhs, socket const & rhs) { return lhs._sock >  rhs._sock; }

    friend std::ostream & operator<< (std::ostream & os, socket const & s) { return os << "socket(" << s._sock << ')'; }

  protected:
    LOGXX_DEFINE_TARGET(LOGXX_SCOPE_NAME);

  private:
    native_socket_t const       _sock;
    bool                        _close_on_destruction;
  };

}} // namespace ioxx::detail

#endif // IOXX_DETAIL_SOCKET_HPP_INCLUDED_2008_04_20
