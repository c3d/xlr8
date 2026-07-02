#******************************************************************************
# Makefile                                                          XLR8 project
#******************************************************************************
#
#  File Description:
#
#    Testing XLR8 source files using a makefile
#
#    This makes it possible to run tests in paralelle
#
#
#
#
#
#
#******************************************************************************
#  (C) 2021 Christophe de Dinechin <christophe@dinechin.org>
#  This software is licensed under the GNU General Public License v3
#******************************************************************************
#  This file is part of XLR8.
#
#  XLR8 is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  XLR8 is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with XLR8.  If not, see <https://www.gnu.org/licenses/>.
#******************************************************************************

XLR8=./xlr8 $(OPTLEVEL)
TESTS=$(TESTS_$(PERF))
RUN_TESTS=yes
TIMEOUT=30
PERF=
TESTS_perf=$(wildcard */*.xlr8)
TESTS_=$(filter-out performance/%,$(TESTS_perf))
MIQ=../make-it-quick/

# Make sure we don't get "WARNING: Cannot set locale" messages polluting tests
export LANG=C
export LC_ALL=C
export LC_CTYPE=C
export XLR8
export LD_LIBRARY_PATH=..
export DYLD_LIBRARY_PATH=..

include $(MIQ)rules.mk

BASELINE=baseline$(OPTLEVEL).txt

base rebase baseline: $(BASELINE)
run-%:
	$(PRINT_COMMAND) $(MAKE) TESTS="$(wildcard */*$**.xlr8)" .tests
ref-%:
	$(PRINT_COMMAND) $(MAKE) TESTS="$(wildcard */*$**.xlr8)" UPDATE_REFS=yes .tests
perf: perf-tests
perf-%:
	$(PRINT_COMMAND) $(MAKE) PERF=perf $*

XLR8_CHECK=$(if $($1.CHECK), $($1.CHECK) &&)

# Check a command, and set the result accordingly
# $1 = pattern
# $2 = command
define result
( $2 && $(XLR8_CHECK) ( echo "OK" > $1.result )|| ( echo "NO" > $1.result) )
endef


# Check a command, and set the result accordingly
# $1 = pattern
# $2 = command
define command
  $(call result,$1,					\
    ( $2 2>&1 ) ;					\
    test "$$?" -eq "$(if $($1.EXIT),$($1.EXIT),0)"	\
  )
endef

TEST_OK=-f "$1.result" -a "$$($(CAT) $1.result)" == "OK"
TEST_KO=! \( $(TEST_OK) \)

# Set the result based on whether files are identical
# $1 = target
define diff
  if [ $(TEST_OK) ]; then				\
  $(UPDATE_CMD)						\
  $(call result,$1,					\
    diff $(firstword $(wildcard $1$(OPTLEVEL).ref)	\
		     $(wildcard $1.ref))		\
         $1.output > $1.diff 2>&1)			\
  fi
endef

UPDATE_CMD=$(UPDATE_CMD_$(UPDATE_REFS))
UPDATE_CMD_yes=					\
  cp $1.output $1$(OPTLEVEL).ref;		\
  grep -v $1 $(BASELINE) > baseline-new.txt;	\
  mv baseline-new.txt $(BASELINE) ;

# Run a command with a timeout
# $1 = timeout
# $2 = pattern
# $3 = command
define timeout
  (					\
    echo "TIMEOUT" > $2.result;		\
    $(call command,$2,timeout $1 $3)	\
  )
endef

XLR8_CMD= $(if $($1.CMD),$($1.CMD),$(XLR8) $($1.OPT) $1.xlr8)
XLR8_FILT=$(if $($1.FILTER),| ($($1.FILTER) || true))

# Run a simple test
# $1 = pattern
XLR8_TIMEOUT=$(if $($*.TIMEOUT),$($*.TIMEOUT),$(TIMEOUT))
define test
	$(call timeout,$(XLR8_TIMEOUT),$*,$(XLR8_CMD)) $(XLR8_FILT) > $1.output
endef


#------------------------------------------------------------------------------
#  Rules
#------------------------------------------------------------------------------

# Compute variables for the various builds
-include $(TESTS:%=%$(OPTLEVEL).mk)

.tests: failures.log

GENERATE_FAILURES_LOG=						\
  ( for F in $(^:.diff=); do					\
    if [ $(call TEST_KO,$$F) ] ; then				\
      if ! grep -q "$$F" $(BASELINE); then			\
	echo "=== $$F FAILED ===";				\
	( echo "--- $$F DIFF ---";				\
	  [ -f "$$F.diff" ] && $(CAT) "$$F.diff" ;		\
	  echo "--- $$F OUTPUT ---";				\
	  [ -f "$$F.output" ] && $(CAT) "$$F.output"		\
	) > "$$F.err";						\
	$(CAT) "$$F.err";					\
      fi;							\
    fi;								\
  done;								\
  for F in $(^:.diff=); do					\
    if [ $(call TEST_KO,$$F) ]; then				\
      if grep -q "$$F" $(BASELINE); then			\
	echo "=== $$F FAILED (EXPECTED) ===";			\
	( echo "--- $$F DIFF ---";				\
	  [ -f "$$F.diff" ] && $(CAT) "$$F.diff" ;		\
	  echo "--- $$F OUTPUT ---";				\
	  [ -f "$$F.output" ] && $(CAT) "$$F.output"		\
	) > "$$F.err";						\
	$(CAT) "$$F.err";					\
      fi;							\
    fi;								\
  done;								\
  for F in $(^:.diff=); do					\
    if [ $(call TEST_OK,$$F) ]; then				\
      if grep -q "$$F" $(BASELINE); then			\
	echo "=== $$F IMPROVED ===";				\
      else							\
	echo "=== $$F SUCCEEDED ===";				\
      rm -f "$$F.err";						\
      fi;							\
    fi;								\
  done ) > $@ ;							\
  $(REPORT_FAILURES)

REPORT_FAILURES=						\
 (failures=$$(grep "=== .* FAILED ===" $@ | wc -l);		\
  successes=$$(grep "=== .* SUCCEEDED ===" $@ | wc -l);		\
  expected=$$(grep "=== .* FAILED .EXPECTED. ===" $@ | wc -l);	\
  improved=$$(grep "=== .* IMPROVED ===" $@ | wc -l);		\
  if [ "$$failures" -ne "0" ]; then				\
    $(INFO) "[SUMMARY]" "=== TESTS FAILED ===" "[NO]";		\
    grep "=== .* FAILED ===" $@;				\
  elif [ "$$improved" -ne "0" ]; then				\
    $(INFO) "[SUMMARY]" "=== TESTS IMPROVED ===" "[++]";	\
    grep "=== .* IMPROVED ===" $@;				\
  else								\
    $(INFO) "[SUMMARY]" "=== TESTS PASSED ===" "[OK]";		\
  fi;								\
  $(INFO) "[SUMMARY]" "  Successful tests:  $$successes";	\
  $(INFO) "[SUMMARY]" "  Failed tests:      $$failures";	\
  $(INFO) "[SUMMARY]" "  Improved tests:    $$improved";	\
  $(INFO) "[SUMMARY]" "  Expected failures: $$expected";	\
  ./timing-results;						\
  if [ "$$failures" -ne "0" ]; then				\
    head -20 $@ ;						\
    exit 41 ;							\
  fi)

GENERATE_BASELINE=						\
  grep "=== .* FAILED .* ===" $< |				\
  sed -e 's/=== \(.*\) FAILED\(.*\) ===/\1.xlr8/g' > $@


failures.log: $(TESTS:%.xlr8=%.diff)
	$(PRINT_GENERATE) $(GENERATE_FAILURES_LOG) $(COLORIZE)
$(BASELINE): failures.log
	$(PRINT_GENERATE) $(GENERATE_BASELINE) $(COLORIZE)

%.xlr8$(OPTLEVEL).mk: %.xlr8 ./alltests-make.awk # Makefile
	$(PRINT_GENERATE) ./alltests-make.awk $< > $@

%.xlr8.test: %.diff
	$(PRINT_RESULT)

%.diff: %.output %.ref
	$(PRINT_COMMAND) $(call diff,$*)

%.output: %.xlr8 .products
	$(PRINT_COMMAND) $(call test,$*)

XLR8_NOREF_MSG=$(if $(UPDATE_REFS),"OK","REF?")
XLR8_NOREF=							\
	echo $(XLR8_NOREF_MSG) > $*.result	;		\
	$(INFO) "[WARNING]" "No reference for $*" "[REF?]"

%.ref:
	$(PRINT_COMMAND) $(XLR8_NOREF) $(COLORIZE)

xlr8.exe=	$(PFX.exe)xlr8$(EXT.exe)
xlr8.dll=	$(PFX.dll)xlr8$(EXT.dll)
recorder.dll=	$(PFX.dll)recorder$(EXT.dll)

XLR8_BINARIES=				\
	$(xlr8.exe)			\
	$(xlr8.dll)			\
	$(recorder.dll)

# XLR8_FILES= 				\
#	syntax.xlr8

.products:	$(XLR8_BINARIES) $(XLR8_FILES)

define link

$1: $2/$1
	$$(PRINT_COPY)	ln -sf $$< $$@

endef

$(eval $(foreach b,$(XLR8_BINARIES),$(call link,$b,..)))
$(eval $(foreach f,$(XLR8_FILES),$(call link,$f,../src)))

../$(xlr8.dll)		: ../$(xlr8.exe)
../$(recorder.dll)	: ../$(xlr8.exe)
../$(xlr8.exe):
	$(PRINT_BUILD) cd .. && make

.PRECIOUS: %.diff %.output %.ref
.PHONY: %.xlr8.test
