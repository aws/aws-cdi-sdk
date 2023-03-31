top := $(abspath $(CURDIR)/..)

## definitions common to library and test
# intersection point of directories where build process takes place
top.sdk := $(abspath $(CURDIR)/)

# all SDK build artifacts go into a debug or release directory under top.build
top.build := $(top.sdk)/build

### The AWS SDK is a required dependency of the CDI SDK by default for the metrics gathering service and for publishing
### metrics to CloudWatch. This requirement can be removed by building with NO_MONITORING=y (not recommended).
ifeq ($(NO_MONITORING),)
    require_aws_sdk := yes
else
    require_aws_sdk := no
endif
require_aws_sdk_build := $(require_aws_sdk)

#-----------------------------------------------------------------------------
# Get product name and version information from text file.
#-----------------------------------------------------------------------------

define version_parser_script
BEGIN { name = "CDI" ; version = 0 ; major = 0 ; minor = 0 ; FS = " " }
$$1 == "#define" && $$2 ~ "^CDI_SDK.*_VERSION$$" { split($$2, a, /_/) ; num = $$3 ;
if (a[3] == "VERSION") { version = num } else if (a[3] == "MAJOR") { major = num } else if (a[3] == "MINOR") { minor = num } }
END { printf("name_and_version_string := %s %d %d %d %d\n", name, version, major, minor, subminor) }
endef
$(eval $(shell awk '$(version_parser_script)' $(top.sdk)/include/cdi_core_api.h))
product_name := $(word 1,$(name_and_version_string))
product_version := $(word 2,$(name_and_version_string))
product_major_version := $(word 3,$(name_and_version_string))
product_minor_version := $(word 4,$(name_and_version_string))
product_version_name := "$(product_version)_$(product_major_version)_$(product_minor_version)"

libcdisdk_version_string := $(shell head libcdisdk.vers -n 1 | cut -d' ' -f 1)
expected_libcdisdk_version_string := VERS_$(product_version).$(product_major_version)

ifneq ($(libcdisdk_version_string),$(expected_libcdisdk_version_string))
    $(error Release version in libcdisdk.vers does not match code)
endif

# Enable, if desired to see console output.
# $(info Name=$(product_name))
#$(info Version=$(product_version))
#$(info Major=$(product_major_version))
#$(info Minor=$(product_minor_version))

# top level directories for library and test program
top.src := $(top.sdk)/src
top.common := $(top.src)/common
top.tests := $(top.sdk)/tests
top.test_common := $(top.src)/test_common
top.test_minimal := $(top.src)/test_minimal
top.test_unit := $(top.src)/test_unit

### Obtain a list of the real build goals, if any, to determine whether some dependencies apply or not.
real_build_goals := $(strip $(filter-out clean% docs% headers help,$(if $(MAKECMDGOALS),$(MAKECMDGOALS),none)))

# header directories used by both library and test program
include_dir.sdk := $(top.sdk)/include
include_dir.sdk += $(top.src)/libfabric_api
include_dir.common := $(top.common)/include

## optional sanitizer settings
cc_major_version := $(shell $(CC) -dumpversion | cut -c1)
sanitize_styles.4 := -fsanitize=address
sanitize_styles.7 := -fsanitize=address -fsanitize=leak -fsanitize=bounds-strict -fstack-check -fno-omit-frame-pointer
sanitize_opts := $(if $(filter y%,$(SANITIZE)),$(sanitize_styles.$(cc_major_version)))
# SANITIZE=y implies DEBUG=y
ifneq ($(sanitize_opts),)
    override DEBUG=y
    ASAN_LIBS := -lasan -lubsan
endif

## Check for GCC version 4.8.5 initialization bug
GCCVERSION = $(word 1,$(shell $(CC) --version | grep -o '[0-9]*\.[0-9]*\.[0-9]*'))

ifeq "$(GCCVERSION)" "4.8.5"
    $(error GCC 4.8.5 has a bug causing 'missing braces around initalizer' errors. For more information see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119.)
endif

ifeq ($(DEBUG), y)
    config := debug
    config_libfabric := debug
    config_aws_sdk := Debug
else
    config := release
    config_libfabric := release
    config_aws_sdk := Release
endif

## outputs from compilation process
# this is the config specific directory for build artifacts
build_dir := $(top.build)/$(config)
# object files go into obj dir, libraries under lib, and programs in bin
build_dir.obj := $(build_dir)/obj
build_dir.lib := $(build_dir)/lib
build_dir.lib64 := $(build_dir)/lib64
build_dir.bin := $(build_dir)/bin
build_dir.results := $(build_dir)/results
build_dir.image := $(build_dir)/image
build_dir.packages := $(build_dir)/packages
build_dir.libaws := $(build_dir)/libaws

# Setup path to libfabric 1.9 and libfabric_new
ifneq ($(wildcard $(top)/libfabric),)
    top.libfabric := $(top)/libfabric
endif
ifneq ($(wildcard $(top)/libfabric_new),)
    top.libfabric_new := $(top)/libfabric_new
endif

# Users can add their own variables to this makefile by creating a makefile in this directory called
# makefile.[name].vars.mk.
-include makefile.*.vars.mk

ifeq ($(top.libfabric),)
    $(error libfabric source tree not found)
endif
ifeq ($(top.libfabric_new),)
    $(error libfabric_new source tree not found)
endif

# Build artifacts for libfabric go into a debug or release directory under top.libfabric
build_dir.libfabric := $(top.libfabric)/build/$(config_libfabric)
build_dir.libfabric_new := $(top.libfabric_new)/build/$(config_libfabric)

libfabric_config_h := $(build_dir.libfabric)/config.h
libfabric_new_config_h := $(build_dir.libfabric_new)/config.h

## library build definitions
# "top level" subtrees
top.cdi := $(top.src)/cdi

# all of the directories with C source files in them
src_dir.cdi := $(top.cdi)
src_dir.common := $(top.common)/src
src_dir.libfabric_api := $(top.src)/libfabric_api
src_dir.test := $(top.src)/test
src_dir.test_common := $(top.test_common)/src
src_dir.test_minimal := $(top.test_minimal)
src_dir.test_unit := $(top.test_unit)
src_dir.tools := $(top.src)/tools
ifeq ($(require_aws_sdk),yes)
src_extensions := c cpp
else
src_extensions := c
endif

# directories with header files
include_dir.cdi := $(top.cdi)
include_dirs.cdi := $(foreach dir,sdk cdi common,$(include_dir.$(dir)))
include_dirs.libfabric := $(top.libfabric)/include $(build_dir.libfabric)
include_dirs.libfabric_new := $(top.libfabric_new)/include $(build_dir.libfabric_new)

# generate various lists for building SDK library
srcs.cdi := $(foreach ext,$(src_extensions),$(wildcard $(src_dir.cdi)/*.$(ext)))
srcs.cdi += queue.c fifo.c list.c logger.c os_linux.c pool.c
objs.cdi := $(addprefix $(build_dir.obj)/,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(notdir $(srcs.cdi)))))
headers.cdi := $(foreach dir,$(include_dirs.cdi),$(wildcard $(dir)/*.h))
include_opts.cdi := $(foreach proj,cdi,$(addprefix -I,$(include_dirs.$(proj))))
include_opts.libfabric := $(foreach proj,libfabric,$(addprefix -I,$(include_dirs.$(proj))))
include_opts.libfabric_new := $(foreach proj,libfabric_new,$(addprefix -I,$(include_dirs.$(proj))))
depends.cdi := $(patsubst %.o,%.d,$(objs.cdi))

# the end goal of building the SDK library
libsdk := $(build_dir.lib)/libcdisdk.so.$(product_version).$(product_major_version)

# the end goal of building the CDI libfabric API libraries
libfabric_api := $(build_dir.lib)/libcdi_libfabric_api.so.1
libfabric_api_new := $(build_dir.lib)/libcdi_libfabric_api_new.so.1

# generate various lists for building libfabric API libraries
srcs.libfabric_api := $(src_dir.libfabric_api)/libfabric_api.c
objs.libfabric_api := $(addprefix $(build_dir.obj)/,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(notdir $(srcs.libfabric_api)))))
objs.libfabric_api_new := $(build_dir.obj)/libfabric_api_new.o
headers.libfabric_api := $(foreach dir,$(include_dirs.libfabric_api),$(wildcard $(dir)/*.h))
depends.libfabric_api := $(patsubst %.o,%.d,$(objs.libfabric_api))

# the end goal of building the libfabric shared libraries
libfabric := $(build_dir.lib)/libfabric.so.1
libfabric_new := $(build_dir.lib)/libfabric_new.so.1

## test program definitions
# test specific source files are all in one directory
include_dir.test := $(src_dir.test)
include_dir.test_common := $(top.test_common)/include
include_dirs.test := $(foreach dir,sdk test common test_common,$(include_dir.$(dir)))

# generate lists for building cdi_test program
srcs.test := $(wildcard $(src_dir.test)/*.c) $(wildcard $(src_dir.test_common)/*.c)
objs.test := $(addprefix $(build_dir.obj)/,$(patsubst %.c,%.o,$(notdir $(srcs.test))))
headers.test := $(foreach dir,$(include_dirs.test),$(wildcard $(dir)/*.h))
include_opts.test := $(addprefix -I,$(include_dirs.test))
depends.test := $(patsubst %.o,%.d,$(objs.test))

# the end goal of building cdi_test program
test_program := $(build_dir.bin)/cdi_test

# generate lists for building cdi_test_unit program
srcs.test_unit := $(wildcard $(src_dir.test_unit)/*.c) $(wildcard $(src_dir.test_common)/*.c)
objs.test_unit := $(addprefix $(build_dir.obj)/,$(patsubst %.c,%.o,$(notdir $(srcs.test_unit))))
headers.test_unit := $(foreach dir,$(include_dirs.test_unit),$(wildcard $(dir)/*.h))
depends.test_unit := $(patsubst %.o,%.d,$(objs.test_unit))

# the end goal of building cdi_test_unit program
test_unit_program := $(build_dir.bin)/cdi_test_unit

# generate lists for building cdi_test_min_tx programs
srcs.test_min_tx := $(src_dir.test_minimal)/test_minimal_transmitter.c $(wildcard $(src_dir.test_common)/*.c)
objs.test_min_tx := $(addprefix $(build_dir.obj)/,$(patsubst %.c,%.o,$(notdir $(srcs.test_min_tx))))
headers.test_min_tx := $(foreach dir,$(include_dirs.test_min_tx),$(wildcard $(dir)/*.h))
depends.test_min_tx := $(patsubst %.o,%.d,$(objs.test_min_tx))

# the end goal of building cdi_test_min_tx program
test_min_tx_program := $(build_dir.bin)/cdi_test_min_tx

# generate lists for building cdi_test_min_rx program
srcs.test_min_rx := $(src_dir.test_minimal)/test_minimal_receiver.c $(wildcard $(src_dir.test_common)/*.c)
objs.test_min_rx := $(addprefix $(build_dir.obj)/,$(patsubst %.c,%.o,$(notdir $(srcs.test_min_rx))))
headers.test_min_rx := $(foreach dir,$(include_dirs.test_min_rx),$(wildcard $(dir)/*.h))
depends.test_min_rx := $(patsubst %.o,%.d,$(objs.test_min_rx))

# the end goal of building cdi_test_min_rx program
test_min_rx_program := $(build_dir.bin)/cdi_test_min_rx

# generate lists for building the dump_riff program
srcs.dump_riff := $(src_dir.test)/riff.c $(wildcard $(src_dir.tools)/*.c) $(wildcard $(src_dir.test_common)/*.c)
objs.dump_riff := $(addprefix $(build_dir.obj)/,$(patsubst %.c,%.o,$(notdir $(srcs.dump_riff))))
headers.dump_riff := $(include_dir.test)/riff.h
depends.dump_riff := $(patsubst %.o,%.d,$(objs.dump_riff))

# the goal of building the dump_riff program
dump_riff_program := $(build_dir.bin)/dump_riff

# all of the header files, used only for "headers" target
headers.all := $(foreach dir,cdi test test_common test_min_tx test_min_rx test_unit,$(headers.$(dir)))

# augment compiler flags
COMMON_COMPILER_FLAG_ADDITIONS := \
	$(include_opts.cdi) $(include_opts.test) \
	$(EXTRA_COMPILER_FLAG_ADDITIONS) \
	-Wall -Wextra -Werror -pthread -fPIC \
	-D_LINUX -D_POSIX_C_SOURCE=200112L \
	$(sanitize_opts)
ifeq ($(config), debug)
    COMMON_COMPILER_FLAG_ADDITIONS += -O0 -g -DDEBUG
else
    COMMON_COMPILER_FLAG_ADDITIONS += -O3 -DNDEBUG
endif

ifneq ($(NO_MONITORING),)
    COMMON_COMPILER_FLAG_ADDITIONS += -DCDI_NO_MONITORING
endif

CFLAGS += $(COMMON_COMPILER_FLAG_ADDITIONS) --std=c99
CXXFLAGS += $(COMMON_COMPILER_FLAG_ADDITIONS) --std=c++11

# additional flags to pass to the linker to create cdi_test* programs
# The only libraries needed here are those that present new dependencies beyond what libcdisdk.so already requires.
# An rpath is specified so cdi_test can find libcdisdk.so.x in the same directory as cdi_test or in a sibling directory
# named lib.
CDI_LDFLAGS = $(LDFLAGS) -L$(build_dir.lib) -lcdisdk $(EXTRA_LD_LIBS) -lm $(aws_sdk_library_flags)
CDI_TEST_LDFLAGS = -lncurses -ltinfo -Wl,-rpath,\$$ORIGIN:\$$ORIGIN/../lib64:\$$ORIGIN/../lib -Wl,-rpath-link,$(build_dir.lib)

# docs go into the build directory but are not specific to release/debug
build_dir.doc := $(top.build)/documentation

# set "don't print" according to the value of V
V ?= 0  # not verbose by default; V can be overridden on the command line
Q-0 := @
Q-1 :=
Q := $(Q-$(strip $(V)))

# If verbose mode is set, then enable more logging when running tests.
ifneq ($(strip $(V)), 0)
    test_logging := --show-job-log
endif

# Determine CMake executable
ifneq ($(shell command -v cmake3;),)
    CMAKE := $(shell command -v cmake3;)
else
    CMAKE := $(shell command -v cmake;)
endif

ifeq ($(CMAKE),)
    $(error CMake not found. See INSTALL instructions.)
endif

# default build target
.PHONY : all
all : libfabric libfabric_new libsdk libfabric_api libfabric_api_new test docs docs_api $(EXTRA_ALL_TARGETS)

# Build only the libraries
.PHONY : lib
lib : libfabric libfabric_new libsdk libfabric_api libfabric_api_new

# Ensure that the location of the AWS SDK source code tree was specified unless explicitly opted out. Define and augment
# some variables needed for building and linking to the AWS SDK.
ifeq ($(require_aws_sdk_build),yes)
    ifneq ($(real_build_goals),)
        ifeq ($(AWS_SDK),)
            $(error AWS_SDK must be specified.)
        else
            AWS_SDK_ABS := $(abspath $(AWS_SDK))
            ifeq (0,$(shell if [ -d $(AWS_SDK_ABS)/src/aws-cpp-sdk-core ]; then echo 1; else echo 0; fi))
                $(error AWS_SDK does not point to the root of the AWS SDK.)
            else
                libaws := $(foreach component,monitoring core cdi,$(build_dir.lib64)/libaws-cpp-sdk-$(component).so)
                aws_sdk_library_flags = -L$(build_dir.lib) -L$(build_dir.lib)64
                # add necessary flags for compiler and linker
                CXXFLAGS += -I$(build_dir)/include
                aws_h := $(build_dir)/include/aws/core/Aws.h
                cdi_sdk_src := $(AWS_SDK_ABS)/generated/src/aws-cpp-sdk-cdi
            endif
        endif
    endif
endif

ifeq ($(require_aws_sdk),yes)
    CDI_LDFLAGS += -laws-checksums -laws-c-event-stream
    aws_sdk_library_flags += -laws-cpp-sdk-cdi -laws-cpp-sdk-core -laws-cpp-sdk-monitoring -lstdc++
endif

.PHONY : help
help ::
	@echo "Build targets:"
	@echo "    all [default]  - Includes libraries, test program, docs."
	@echo "    lib            - Builds only the libraries."
	@echo "    test           - Builds test programs (cdi_test, cdi_test_min_tx, cdi_test_min_rx, cdi_test_unit)."
	@echo "    docs           - Generates all HTML documentation from embedded Doxygen comments."
	@echo "    docs_api       - Generates only API HTML documentation from embedded Doxygen comments."
	@echo "    clean          - Removes all build artifacts (debug and release)."
	@echo "    cleanall       - clean and removes libfabric artifacts."
	@echo "    headers        - Test compiles header files for correctness."
	@echo ""
	@echo "Required parameters:"
	@echo "    AWS_SDK=<path> - Path to AWS SDK; specifying this enables publishing CloudWatch metrics."
	@echo ""
	@echo "Options:"
	@echo "    DEBUG=y        - Builds debug instead of release."
	@echo "    SANITIZE=y     - Builds debug with extra Sanitizer run-time checking (automatically DEBUG=y)."
	@echo "    V=1            - Echos build commands before executing them."
	@echo ""
	@echo "Output files are placed into build/{debug|release}/{lib|bin}."

# look for .c files in the CDI, test and tool source directories.
#
# NOTE: the vpath function does not support ambiguity of files with the same name in different directories. Therefore
# all .c files used in this project MUST have unique names.
vpath %.c $(foreach proj,cdi common test test_common test_minimal test_unit tools,$(src_dir.$(proj)))
ifeq ($(require_aws_sdk),yes)
vpath %.cpp $(src_dir.cdi)
endif

# rule to create the various build output directories
$(foreach d,obj lib bin doc packages libfabric libfabric_new results image libaws,$(build_dir.$(d))) :
	$(Q)mkdir -p $@

# Setup flags for libfabric depending on debug/release build target.
LIBFABRIC_OPTS := --prefix=$(build_dir.libfabric) \
	--enable-efa=yes \
	--srcdir=$(top.libfabric) \
	--disable-verbs \
	--disable-rxd
ifeq ($(config), debug)
    LIBFABRIC_OPTS += --enable-debug
endif

# Allows rdma-core path to be defined outside of this Makefile.
RDMA_CORE_PATH := yes
LIBFABRIC_NEW_OPTS := --prefix=$(build_dir.libfabric_new) \
	--enable-efa=$(RDMA_CORE_PATH) \
	--srcdir=$(top.libfabric_new) \
	--disable-verbs \
	--disable-rxd
ifeq ($(config), debug)
    LIBFABRIC_NEW_OPTS += --enable-debug
endif

$(libfabric_config_h) : | $(build_dir.libfabric)
	@echo "Configuring libfabric. Creating $(libfabric_config_h)"
	$(Q)cd $(top.libfabric)       && \
	    ./autogen.sh              && \
	    cd $(build_dir.libfabric) && \
	    $(top.libfabric)/configure $(LIBFABRIC_OPTS)

$(libfabric_new_config_h) : | $(build_dir.libfabric_new)
	@echo "Configuring libfabric_new. Creating $(libfabric_new_config_h)"
	$(Q)cd $(top.libfabric_new)       && \
	    ./autogen.sh                       && \
	    cd $(build_dir.libfabric_new) && \
	    $(top.libfabric_new)/configure $(LIBFABRIC_NEW_OPTS)

# rule to create the libfabric library files.
# NOTE: The build steps used here were created from the libfabric docs.
# Implementation note: Specifying both "all" and "install" in the same make command fails if -j is also specified. The
# libfabric Makefile has some kind of issue that requires these to be two distinct invocations.
.PHONY : libfabric
libfabric : $(libfabric)
$(libfabric) : $(libfabric_config_h) | $(build_dir.lib)
	@echo "Building libfabric. Creating output files in $(build_dir.libfabric)"
	$(Q)$(MAKE) -C $(build_dir.libfabric) -j$$(nproc) all
	$(Q)$(MAKE) -C $(build_dir.libfabric) install
	$(Q)cp $(build_dir.libfabric)/lib/$(notdir $(libfabric)) $(libfabric)
	$(Q)ln -fs $@ $(basename $@)
	$(Q)ln -fs $@ $(basename $(basename $@))

# rule to create the libfabric_new library files.
# NOTE: The build steps used here were created from the libfabric docs.
# Implementation note: Specifying both "all" and "install" in the same make command fails if -j is also specified. The
# libfabric Makefile has some kind of issue that requires these to be two distinct invocations.
.PHONY : libfabric_new
libfabric_new : $(libfabric_new)
$(libfabric_new) : $(libfabric_new_config_h) | $(build_dir.lib)
	@echo "Building libfabric_new. Creating output files in $(build_dir.libfabric_new)"
	$(Q)$(MAKE) -C $(build_dir.libfabric_new) -j$$(nproc) all
	$(Q)$(MAKE) -C $(build_dir.libfabric_new) install
	$(Q)cp $(build_dir.libfabric_new)/lib/$(notdir $(libfabric)) $(libfabric_new)
	$(Q)ln -fs $@ $(basename $@)
	$(Q)ln -fs $@ $(basename $(basename $@))

# rule to create the SDK library file
.PHONY : libsdk
libsdk : $(libsdk)
ifeq ($(require_aws_sdk),yes)
$(libsdk) : $(libfabric_config_h) $(objs.cdi) $(libfabric_api) $(libfabric_api_new) $(libaws) | $(build_dir.lib)
else
$(libsdk) : $(libfabric_config_h) $(objs.cdi) $(libfabric_api) $(libfabric_api_new) | $(build_dir.lib)
endif
	@echo "GCC version is" $(GCCVERSION)
	$(Q)$(CC) -shared -o $@ -Wl,-z,defs,-soname=$(basename $(notdir $@)),--version-script,libcdisdk.vers \
		$(objs.cdi) -L$(build_dir.lib) $(aws_sdk_library_flags) \
		-lcdi_libfabric_api -lcdi_libfabric_api_new -ldl -lrt $(EXTRA_CC_LIBS) -lm $(EXTRA_LD_LIBS) -lpthread -lc \
		$(ASAN_LIBS) -Wl,-rpath,\$$ORIGIN:\$$ORIGIN/lib
	$(Q)ln -fs $@ $(basename $@)
	$(Q)ln -fs $@ $(basename $(basename $@))

# rule to create libfabric 1.9 API library file
.PHONY : libfabric_api
libfabric_api : $(libfabric_api)
$(libfabric_api) : $(libfabric_config_h) $(objs.libfabric_api) $(libfabric) | $(build_dir.lib)
	$(Q)$(CC) -shared -o $@ -Wl,-z,defs,-soname=$(basename $(notdir $@)) \
		$(objs.libfabric_api) -L$(build_dir.lib) \
		-ldl -lrt $(EXTRA_CC_LIBS) -lm $(EXTRA_LD_LIBS) -lpthread -lc \
		$(ASAN_LIBS)
	$(Q)ln -fs $@ $(basename $@)
	$(Q)ln -fs $@ $(basename $(basename $@))

# rule to create libfabric new API library file
.PHONY : libfabric_api_new
libfabric_api_new : $(libfabric_api_new)
$(libfabric_api_new) : $(libfabric_new_config_h) $(objs.libfabric_api_new) $(libfabric_new) | $(build_dir.lib)
	$(Q)$(CC) -shared -o $@ -Wl,-z,defs,-soname=$(basename $(notdir $@)) \
		$(objs.libfabric_api_new) -L$(build_dir.lib) \
		-ldl -lrt $(EXTRA_CC_LIBS) -lm $(EXTRA_LD_LIBS) -lpthread -lc \
		$(ASAN_LIBS)
	$(Q)ln -fs $@ $(basename $@)
	$(Q)ln -fs $@ $(basename $(basename $@))

# rule to build libfabric_1_9_api.o and libfabric_new_api.o from libfabric_1_9_api.c
$(objs.libfabric_api) $(objs.libfabric_api_new) : $(srcs.libfabric_api) | $(build_dir.obj)
	@echo "Compiling libfabric_api.c using libfabric 1.9."
	$(Q)$(CC) $(include_opts.libfabric) $(CFLAGS) -c -o $(objs.libfabric_api) $<
	@echo "Compiling libfabric_api.c using libfabric new."
	$(Q)$(CC) -DLIBFABRIC_NEW $(include_opts.libfabric_new) $(CFLAGS) -c -o $(objs.libfabric_api_new) $<

# rule to create symlink to CDI monitoring service client source code
ifneq ($(cdi_sdk_src),)
$(cdi_sdk_src) :
	$(Q)ln -fs $(top.sdk)/$(notdir $@) $@
endif

# rule to build the AWS SDK
$(aws_h) $(libaws) : $(cdi_sdk_src) | $(build_dir.libaws)
	$(Q)cd $(build_dir.libaws) \
	    && $(CMAKE) -DCMAKE_BUILD_TYPE=$(config_aws_sdk) -DBUILD_ONLY="monitoring;cdi" \
	           -DCMAKE_INSTALL_PREFIX=$(build_dir) $(AWS_SDK_ABS) \
	           -DCMAKE_VERBOSE_MAKEFILE=TRUE \
	           -DAUTORUN_UNIT_TESTS=FALSE \
	           -DENABLE_TESTING=FALSE \
	    && $(MAKE) -j $$(nproc) V=$(V) \
	    && $(MAKE) install V=$(V)
ifneq ($(aws_h),)
		$(Q)touch $(aws_h)
endif

# rule to build a .d file from a .c file; relies on vpath to find the source file
$(build_dir.obj)/%.d : %.c | $(build_dir.obj)
	$(Q)$(CC) $(include_opts.libfabric) $(CFLAGS) -MM -MF $@ -MT $(patsubst %.d,%.o,$@) $<

# rule to build a .o file from a .c file; relies on vpath to find the source file
$(build_dir.obj)/%.o : %.c | $(build_dir.obj)
	$(Q)$(CC) $(include_opts.libfabric) $(CFLAGS) -c -o $@ $<

ifeq ($(require_aws_sdk),yes)
# Define a dependency to cause the AWS SDK to get built prior to trying to compile cw_metrics.cpp.
$(build_dir.obj)/cloudwatch_sdk_metrics.o : $(aws_h)

# rule to build a .d file from a .c file; relies on vpath to find the source file
$(build_dir.obj)/%.d : %.cpp | $(build_dir.obj)
	$(Q)$(CXX) $(CXXFLAGS) -MM -MF $@ -MT $(patsubst %.d,%.o,$@) $<

# rule to build a .o file from a .c file; relies on vpath to find the source file
$(build_dir.obj)/%.o : %.cpp | $(build_dir.obj)
	$(Q)$(CXX) $(CXXFLAGS) -c -o $@ $<
endif

# rules for building the test programs
.PHONY : test
test : $(test_program) $(test_min_tx_program) $(test_min_rx_program) $(test_unit_program)
$(test_program) : $(objs.test) $(libsdk) | $(build_dir.bin)
	@echo "Linking $(notdir $@) with shared library in $(libsdk)"
	$(Q)$(CC) $(CFLAGS) -o $@ $(objs.test) $(CDI_LDFLAGS) $(CDI_TEST_LDFLAGS)

$(test_min_tx_program) : $(objs.test_min_tx) $(libsdk) | $(build_dir.bin)
	@echo "Linking $(notdir $@) with shared library in $(libsdk)"
	$(Q)$(CC) $(CFLAGS) -o $@ $(objs.test_min_tx) $(CDI_LDFLAGS) $(CDI_TEST_LDFLAGS)

$(test_min_rx_program) : $(objs.test_min_rx) $(libsdk) | $(build_dir.bin)
	@echo "Linking $(notdir $@) with shared library in $(libsdk)"
	$(Q)$(CC) $(CFLAGS) -o $@ $(objs.test_min_rx) $(CDI_LDFLAGS) $(CDI_TEST_LDFLAGS)

$(test_unit_program) : $(objs.test_unit) $(libsdk) | $(build_dir.bin)
	@echo "Linking $(notdir $@) with shared library in $(libsdk)"
	$(Q)$(CC) $(CFLAGS) -o $@ $(objs.test_unit) $(CDI_LDFLAGS) $(CDI_TEST_LDFLAGS)

# rules for building the tool programs
.PHONY : tools
tools : $(dump_riff_program)
$(dump_riff_program) : $(objs.dump_riff) | $(build_dir.bin)
	@echo "Linking $(notdir $@) with shared library in $(libsdk)"
	$(Q)$(CC) $(CFLAGS) -o $@ $(objs.dump_riff) $(CDI_LDFLAGS) $(CDI_TEST_LDFLAGS)

# how to build the docs
docs_dir.docs := $(build_dir.doc)/all
docs_dir.docs_api := $(build_dir.doc)/api
docs_exclude.docs := aws-cpp-sdk-cdi src/common/src/os_windows.c
docs_exclude.docs_api := $(docs_exclude.docs) src
docs_enabled_sections.docs := ALL_DOCS
docs_enabled_sections.docs_api :=
docs_top_index := $(build_dir.doc)/index.html

.PHONY : docs docs_api

define build_doxygen_index
$(Q)doc_dir="$(docs_dir.$@)" doxygen_exclude="$(docs_exclude.$@)" doxygen_enabled_sections="$(docs_enabled_sections.$@)" doxygen doc/Doxyfile 2>$(build_dir.doc)/doxy_log.txt
$(Q)if [ -s $(build_dir.doc)/doxy_log.txt ]; then echo "Doxygen errors!"; cat $(build_dir.doc)/doxy_log.txt; rm $(build_dir.doc)/doxy_log.txt; exit 1 ; fi;
$(Q)rm $(build_dir.doc)/doxy_log.txt
endef

$(docs_top_index) : $(top.sdk)/doc/index.html | $(build_dir.doc)
	$(Q)cp $< $@

docs : $(docs_top_index)
	$(build_doxygen_index)

docs_api_directory := $(product_name)_api_docs_html_$(product_version_name)
docs_api : $(docs_top_index) | $(build_dir.packages)
	$(build_doxygen_index)
	$(Q)tar --transform "s,^,$(docs_api_directory)/," -C $(build_dir.doc)/api/html -zcvf $(build_dir.packages)/$(docs_api_directory).tar.gz .

# test compile each of the header files then remove the precompiled header file produced
.PHONY : headers
headers : $(headers.cdi)
	$(Q)for i in $^; do \
	       echo "compiling $$(basename $$i)"; \
	       $(CC) $(include_opts.libfabric) $(CFLAGS) $$i; \
	       $(CXX) $(CXXFLAGS) $$i; \
	    done
	$(Q)$(RM) $(addsuffix .gch,$+)

# Clean removes the build directory tree. Cleanall also invokes "make clean" in the libfabric folder.
.PHONY : clean cleanall
clean ::
	$(Q)$(RM) -rf $(top.build)/debug
	$(Q)$(RM) -rf $(top.build)/release

cleanall :: clean
	$(Q)$(RM) -r $(top.libfabric)/build/* $(libfabric_config_h)
	$(Q)$(RM) -r $(top.libfabric_new)/build/* $(libfabric_new_config_h)

$(depends.cdi) : $(libfabric_config_h) $(libfabric_new_config_h) $(aws_h)

# include dependency rules from generated files; this is conditional so .d files are only created if needed.
ifneq ($(real_build_goals),)
-include $(foreach proj,cdi test test_min_tx test_min_rx test_unit dump_riff,$(depends.$(proj)))
endif

# Users can add their own rules to this makefile by creating a makefile in this directory called
# makefile.[name].rules.mk.
-include makefile.*.rules.mk
