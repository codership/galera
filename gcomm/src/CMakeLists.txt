#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

add_library(gcomm
  asio_protonet.cpp
  asio_tcp.cpp
  asio_udp.cpp
  conf.cpp
  defaults.cpp
  datagram.cpp
  evs_consensus.cpp
  evs_input_map2.cpp
  evs_message2.cpp
  evs_node.cpp
  evs_proto.cpp
  gmcast.cpp
  gmcast_proto.cpp
  pc.cpp
  pc_proto.cpp
  protonet.cpp
  protostack.cpp
  transport.cpp
  uuid.cpp
  view.cpp
  socket.cpp
  )

# TODO: Fix these.
target_compile_options(gcomm
  PRIVATE
  -Wno-unused-parameter
  -Wno-conversion
  -Wno-overloaded-virtual
  )

target_link_libraries(gcomm galerautilsxx)
