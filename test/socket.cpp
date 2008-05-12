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

#include <ioxx/socket.hpp>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE( cannot_construct_invalid_system_socket )
{
  BOOST_REQUIRE_THROW(ioxx::system_socket(-1), std::invalid_argument);
}
