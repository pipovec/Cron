# Jediny Makefile pre vsetky programy.
#
#   make              zbuilduje vsetko do bin/
#   make PlayersStatAll   zbuilduje jeden program
#   make clean        zmaze obj/ a bin/
#   make -j$(nproc)   paralelny build

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CXXFLAGS += -I. -I/usr/include/postgresql -I./json/include

.DEFAULT_GOAL := all

BINDIR := bin
OBJDIR := obj

PROGRAMS := Players PlayersInfo2 PlayersStatAll PlayersStatNaborNew \
            PlayersStatAktualneOdchody PlayersStatAktualneOdchodyPl \
            PlayersStatDeparturesGermany PlayersWN8v2 KontrolaKlanov Vehicles \
            PlayersVehicleSnapshot PlayersVehicleDelta

STAT_LIBS := lib/Pgsql.cpp lib/SendCurl.cpp lib/PlayerStatProcessor.cpp

# Pre kazdy program: <Name>_SRCS, <Name>_LDLIBS, volitelne <Name>_CXXFLAGS
Players_SRCS  := src/Players.cpp lib/Pgsql.cpp lib/SendCurl.cpp lib/QueryBuilder.cpp lib/PlayerProcessor.cpp
Players_LDLIBS := -lpq -lcurl -lpthread

PlayersInfo2_SRCS   := src/PlayersInfo2.cpp lib/Pgsql.cpp lib/SendCurl.cpp
PlayersInfo2_LDLIBS := -lpq -lcurl

PlayersStatAll_SRCS                    := src/PlayersStatAll.cpp $(STAT_LIBS)
PlayersStatNaborNew_SRCS               := src/PlayersStat.cpp $(STAT_LIBS)
PlayersStatAktualneOdchody_SRCS        := src/PlayersStatAktualneOdchody.cpp $(STAT_LIBS)
PlayersStatAktualneOdchodyPl_SRCS      := src/PlayersStatAktualneOdchodyPl.cpp $(STAT_LIBS)
PlayersStatDeparturesGermany_SRCS      := src/PlayersStatDeparturesGermany.cpp $(STAT_LIBS)
PlayersStatAll_LDLIBS                  := -lpq -lcurl
PlayersStatNaborNew_LDLIBS             := -lpq -lcurl
PlayersStatAktualneOdchody_LDLIBS      := -lpq -lcurl
PlayersStatAktualneOdchodyPl_LDLIBS    := -lpq -lcurl
PlayersStatDeparturesGermany_LDLIBS    := -lpq -lcurl

PlayersWN8v2_SRCS     := src/PlayersWN8v2.cpp lib/Pgsql.cpp lib/SendCurl.cpp \
                         lib/GetPlayersStats.cpp lib/GetPlayersVehicles.cpp
PlayersWN8v2_CXXFLAGS := -O3 -march=native -flto -fPIC
PlayersWN8v2_LDLIBS   := -lpq -lcurl -lpthread

KontrolaKlanov_SRCS     := src/KontrolaKlanov.cpp lib/KontrolaKlanov/ClanData.cpp \
                           lib/KontrolaKlanov/ClanProcessor.cpp lib/SendCurl.cpp lib/Pgsql.cpp
KontrolaKlanov_CXXFLAGS := -O3
KontrolaKlanov_LDLIBS   := -lcurl -lpq -lpthread

Vehicles_SRCS   := src/Vehicles.cpp lib/Pgsql.cpp lib/SendCurl.cpp
Vehicles_LDLIBS := -lpq -lcurl -lpthread

PlayersVehicleSnapshot_SRCS   := src/PlayersVehicleSnapshot.cpp lib/Pgsql.cpp lib/SendCurl.cpp lib/PlayerVehicles.cpp
PlayersVehicleSnapshot_LDLIBS := -lpq -lcurl -lpthread

PlayersVehicleDelta_SRCS   := src/PlayersVehicleDelta.cpp lib/Pgsql.cpp lib/SendCurl.cpp lib/PlayerVehicles.cpp
PlayersVehicleDelta_LDLIBS := -lpq -lcurl -lpthread

# Kazdy program ma vlastny podadresar obj/<Name>/, aby sa nemiesali rozne flagy.
define PROG_template
$(1)_OBJS := $$(patsubst %.cpp,$(OBJDIR)/$(1)/%.o,$$($(1)_SRCS))

$(BINDIR)/$(1): $$($(1)_OBJS)
	@mkdir -p $$(@D)
	$$(CXX) $$(CXXFLAGS) $$($(1)_CXXFLAGS) -o $$@ $$^ $$($(1)_LDLIBS)

$(OBJDIR)/$(1)/%.o: %.cpp
	@mkdir -p $$(@D)
	$$(CXX) $$(CXXFLAGS) $$($(1)_CXXFLAGS) -MMD -MP -c $$< -o $$@

-include $$($(1)_OBJS:.o=.d)
endef

$(foreach p,$(PROGRAMS),$(eval $(call PROG_template,$(p))))

all: $(addprefix $(BINDIR)/,$(PROGRAMS))

$(PROGRAMS): %: $(BINDIR)/%

clean:
	rm -rf $(OBJDIR)

distclean: clean
	rm -rf $(addprefix $(BINDIR)/,$(PROGRAMS))

.PHONY: all clean distclean $(PROGRAMS)
