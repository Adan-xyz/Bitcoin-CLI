comma := ,
CXX      ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -std=gnu++11 -fpermissive -w -pthread

# OpenSSL (1.1 / 3.x)
SSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
SSL_LIBS   := $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto)

# Berkeley DB C++ (4.8 - 5.3). On Replit/Nix it lives in /nix/store; override with DB_PREFIX=/path

DB_INC := $(firstword $(wildcard /nix/store/*-db-5*-dev/include /usr/include/db5.3 /usr/include/db4.8))
DB_LIBDIR := $(firstword $(wildcard $(filter-out %-dev %-doc %-bin %.drv,$(wildcard /nix/store/*-db-5*))/lib))
DB_CFLAGS := $(if $(DB_INC),-I$(DB_INC))
DB_LIBS   := $(if $(DB_LIBDIR),-L$(DB_LIBDIR) -Wl$(comma)-rpath$(comma)$(DB_LIBDIR)) -ldb_cxx

SRCS := util script db net main market irc sha cli
OBJS := $(SRCS:%=obj/%.o)

bitcoin: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(DB_LIBS) $(SSL_LIBS)

obj/%.o: src/%.cpp $(wildcard src/*.h)
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) $(SSL_CFLAGS) $(DB_CFLAGS) -c $< -o $@

clean:
	rm -rf obj bitcoin
