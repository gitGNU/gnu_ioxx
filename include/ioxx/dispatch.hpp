/*
 * Copyright (c) 2010 Peter Simons <simons@cryp.to>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IOXX_DISPATCH_HPP_INCLUDED_2010_02_23
#define IOXX_DISPATCH_HPP_INCLUDED_2010_02_23

#include <ioxx/detail/config.hpp>
#if defined IOXX_HAVE_EPOLL && IOXX_HAVE_EPOLL
#  include <ioxx/detail/epoll.hpp>
#elif defined IOXX_HAVE_POLL && IOXX_HAVE_POLL
#  include <ioxx/detail/poll.hpp>
#elif defined IOXX_HAVE_SELECT && IOXX_HAVE_SELECT
#  include <ioxx/detail/select.hpp>
#else
#  error "No I/O de-multiplexer available for this platform."
#endif
#include <boost/function/function1.hpp>
#include <map>

namespace ioxx
{
  typedef unsigned int seconds_t;

  /**
   * \internal
   *
   * \brief A simple time-event dispatcher.
   */
  template < class Allocator  = std::allocator<void>
           , class Demux      =
#if defined IOXX_HAVE_EPOLL && IOXX_HAVE_EPOLL
                                detail::epoll
#elif defined IOXX_HAVE_POLL && IOXX_HAVE_POLL
                                detail::poll< typename Allocator::template rebind<pollfd>::other
                                            , typename Allocator::template rebind< std::pair<native_socket_t const, size_t> >::other
                                            >
#elif defined(IOXX_HAVE_SELECT) && IOXX_HAVE_SELECT
                                detail::select
#endif
           , class Handler    = boost::function1< void
                                                , typename Demux::socket::event_set
                                                >
           , class HandlerMap = std::map< native_socket_t
                                        , Handler
                                        , std::less<native_socket_t>
                                        , typename Allocator::template rebind< std::pair<native_socket_t const, Handler> >::other
                                        >
           >
  class dispatch : protected Demux
  {
  public:
    typedef Demux                                                                       demux;
    typedef Handler                                                                     handler;
    typedef HandlerMap                                                                  handler_map;
    typedef typename handler_map::iterator                                              iterator;
    typedef typename demux::socket::event_set                                           event_set;

    /**
     * \internal
     *
     * \brief An event-driven socket.
     */
    class socket : public demux::socket
    {
    public:
      typedef typename demux::socket::event_set event_set;
      typedef typename dispatch::handler        handler;

      /**
       * Register a socket in the i/o event dispatcher.
       *
       * \param disp The dispatch object to register this socket in.
       * \param sock The native socket.
       * \param ev   Event set to wait for.
       * \param f    Callback function to invoke when an event occurs.
       */
      socket(dispatch & disp, native_socket_t sock, handler const & f = handler(), event_set ev = demux::socket::no_events)
      : demux::socket(disp, sock, ev)
      {
        BOOST_ASSERT(this->as_native_socket_t() >= 0);
        std::pair<iterator,bool> const r( context()._handlers.insert(std::make_pair(this->as_native_socket_t(), f)) );
        _iter = r.first;
        BOOST_ASSERT(r.second);
      }

      ~socket()
      {
        context()._handlers.erase(_iter);
      }

      void modify(handler const & f)
      {
        _iter->second = f;
      }

      void modify(handler f, event_set ev)
      {
        this->request(ev);
        swap(_iter->second, f);
      }

    protected:
      dispatch & context() { return static_cast<dispatch &>(demux::socket::context()); }

    private:
      iterator  _iter;
    };

    static seconds_t max_timeout() { return demux::max_timeout(); }

    bool empty() const { return _handlers.empty(); }

    void run()
    {
      native_socket_t s;
      event_set ev;
      while (pop_event(s, ev))
      {
        BOOST_ASSERT(s >= 0);
        BOOST_ASSERT(ev != socket::no_events);
        iterator const i( _handlers.find(s) );
        if (i == _handlers.end())
        {
          LOGXX_MSG_TRACE(this->LOGXX_SCOPE_NAME, "ignore events; handler for socket " << s << " does no longer exist");
          continue;
        }
        i->second(ev);         // this is dangerous in case of suicides
      }
    }

    void wait(seconds_t timeout)
    {
      LOGXX_MSG_TRACE(this->LOGXX_SCOPE_NAME, "probe " << _handlers.size() << " sockets; time out after " << timeout << " seconds");
      demux::wait(timeout);
    }

  private:
    handler_map    _handlers;
  };

} // namespace ioxx

#endif // IOXX_DISPATCH_HPP_INCLUDED_2010_02_23
