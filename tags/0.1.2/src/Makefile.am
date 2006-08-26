SUBDIRS = grapple

bin_PROGRAMS = pong2

pong2_DEPENDENCIES = grapple/libgrapple.a

pong2_LDADD = \
$(top_srcdir)/src/grapple/libgrapple.a \
@LIBS@

pong2_SOURCES = \
main.cpp stuff.hpp Framework.cpp Framework.hpp \
Server.cpp Server.hpp \
Client.cpp Client.hpp \
Field.cpp Field.hpp \
Ball.cpp Ball.hpp \
Player.cpp Player.hpp \
Camera.cpp Camera.hpp \
Interface.cpp Interface.hpp \
Buffer.cpp Buffer.hpp
