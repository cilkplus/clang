#===- lib/profile/Makefile.mk ------------------------------*- Makefile -*--===#
#
#                     The LLVM Compiler Infrastructure
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

ModuleName := profile
SubDirs :=

Sources := $(foreach file,$(wildcard $(Dir)/*.c $(Dir)/*.cc),$(notdir $(file)))
ObjNames := $(patsubst %.c,%.o,$(patsubst %.cc,%.o,$(Sources)))
Implementation := Generic

# FIXME: use automatic dependencies?
Dependencies := $(wildcard $(Dir)/*.h)
