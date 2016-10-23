#!/bin/bash

set -e

: ${BUILD_FIREFOX:=1}
: ${BUILD_SPEC:=1}
: ${CLEAN_SPEC:=0}

: ${PATHROOT:="$PWD"}
if [ ! -f "$PATHROOT/autosetup.sh" ]; then
	echo "Please execute from the root of the repository or set PATHROOT" >&2
	exit 1
fi

: ${PATHSPEC:="$PATHROOT/../cpu2006"}
if [ "$BUILD_SPEC" -ne 0 -a ! -f "$PATHSPEC/shrc" ]; then
	echo "Please install SPEC2006 in $PATHSPEC or set the PATHSPEC environment variable to the location of your SPEC2006 install" >&2
	exit 1
fi

source "$PATHROOT/autosetup-benchmarks.inc"

corecount="`grep '^processor' /proc/cpuinfo|wc -l`"

: ${CONFIG_FIXEDCOMPRESSION:=false}
: ${CONFIG_METADATABYTES:=16}
: ${CONFIG_DEEPMETADATA:=false}
: ${CONFIG_DEEPMETADATABYTES:=16}

: ${JOBS="$corecount"}

: ${PATHAUTOSETUP="$PATHROOT/autosetup.dir"}
: ${PATHAUTOPREFIX="$PATHAUTOSETUP/install"}
: ${PATHAUTOOBJ="$PATHAUTOSETUP/obj"}
: ${PATHAUTOSRC="$PATHAUTOSETUP/src"}
: ${PATHAUTOSTATE="$PATHAUTOSETUP/state"}
: ${PATHAUTOPREFIXBASELINE="$PATHAUTOSETUP/install-baseline"}
: ${PATHAUTOPREFIXTYPESAN="$PATHAUTOSETUP/install-typesan"}
: ${PATHLOG="$PATHROOT/autosetup-log.txt"}

: ${VERSIONBASH=bash-4.3}
: ${VERSIONBINUTILS=binutils-2.26.1}
: ${VERSIONCMAKE=cmake-3.4.1}
: ${VERSIONCMAKEURL=v3.4}
: ${VERSIONCOREUTILS=coreutils-8.22}
: ${VERSIONFIREFOX:=47.0}
: ${VERSIONGPERFTOOLS=c46eb1f3d2f7a2bdc54a52ff7cf5e7392f5aa668}
: ${VERSIONLIBUNWIND=libunwind-1.2-rc1}
: ${VERSIONPERL=perl-5.8.8}
: ${VERSIONPERLURL=5.0}

PATHBINUTILS="$PATHAUTOSRC/$VERSIONBINUTILS"

export PATH="$PATHAUTOPREFIX/bin:$PATH"

exec 5> "$PATHLOG"

run()
{
	echo -------------------------------------------------------------------------------- >&5
	echo "command:          $*"               >&5
	echo "\$PATH:            $PATH"            >&5
	echo "\$LD_LIBRARY_PATH: $LD_LIBRARY_PATH" >&5
	echo "working dir:      $PWD"             >&5
	echo -------------------------------------------------------------------------------- >&5
	success=0
	if [ "$logsuffix" = "" ]; then
		"$@" >&5 2>&5 && success=1
	else
		echo "logging to $PATHLOG.$logsuffix.txt" >&5
		"$@" > "$PATHLOG.$logsuffix.txt" 2>&1 && success=1
	fi
	if [ "$success" -ne 0 ]; then
		echo "[done]" >&5
	else
		echo "Command '$*' failed in directory $PWD with exit code $?, please check $PATHLOG for details" >&2
		exit 1
	fi
}

echo "Creating directories"
run mkdir -p "$PATHAUTOSRC"
run mkdir -p "$PATHAUTOSTATE"

export CFLAGS="-I$PATHAUTOPREFIX/include"
export CPPFLAGS="-I$PATHAUTOPREFIX/include"
export LDFLAGS="-L$PATHAUTOPREFIX/lib"

# build bash to override the system's default shell
echo "building bash"
cd "$PATHAUTOSRC"
[ -f "$VERSIONBASH.tar.gz" ] || run wget "http://ftp.gnu.org/gnu/bash/$VERSIONBASH.tar.gz"
[ -d "$VERSIONBASH" ] || run tar xf "$VERSIONBASH.tar.gz"
run mkdir -p "$PATHAUTOOBJ/$VERSIONBASH"
cd "$PATHAUTOOBJ/$VERSIONBASH"
[ -f Makefile ] || run "$PATHAUTOSRC/$VERSIONBASH/configure" --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install
[ -h "$PATHAUTOPREFIX/bin/sh" ] || run ln -s "$PATHAUTOPREFIX/bin/bash" "$PATHAUTOPREFIX/bin/sh"

# build a sane version of coreutils
echo "building coreutils"
cd "$PATHAUTOSRC"
[ -f "$VERSIONCOREUTILS.tar.xz" ] || run wget "http://ftp.gnu.org/gnu/coreutils/$VERSIONCOREUTILS.tar.xz"
[ -d "$VERSIONCOREUTILS" ] || run tar xf "$VERSIONCOREUTILS.tar.xz"
run mkdir -p "$PATHAUTOOBJ/$VERSIONCOREUTILS"
cd "$PATHAUTOOBJ/$VERSIONCOREUTILS"
[ -f Makefile ] || run "$PATHAUTOSRC/$VERSIONCOREUTILS/configure" --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install

# build binutils to ensure we have gold
echo "building binutils"
cd "$PATHAUTOSRC"
[ -f "$VERSIONBINUTILS.tar.bz2" ] || run wget "http://ftp.gnu.org/gnu/binutils/$VERSIONBINUTILS.tar.bz2"
[ -d "$VERSIONBINUTILS" ] || run tar xf "$VERSIONBINUTILS.tar.bz2"
run mkdir -p "$PATHAUTOOBJ/$VERSIONBINUTILS"
cd "$PATHAUTOOBJ/$VERSIONBINUTILS"
confopts="--enable-gold --enable-plugins --disable-werror"
[ -n "`gcc -print-sysroot`" ] && confopts="$confopts --with-sysroot" # match system setting to avoid 'this linker was not configured to use sysroots' error or failure to find libpthread.so
[ -f Makefile ] || run "$PATHAUTOSRC/$VERSIONBINUTILS/configure" --prefix="$PATHAUTOPREFIX" $confopts
run make -j"$JOBS"
run make -j"$JOBS" all-gold
run make install
run rm "$PATHAUTOPREFIX/bin/ld"
(
	echo "#!/bin/bash"
	echo "set -e"
	echo "if [ \"\$USE_GOLD_LINKER\" = 1 ]; then"
	echo "    \"$PATHAUTOPREFIX/bin/ld.gold\" \"\$@\""
	echo "else"
	echo "    \"$PATHAUTOPREFIX/bin/ld.bfd\" \"\$@\""
	echo "fi"
) > "$PATHAUTOPREFIX/bin/ld"
run chmod u+x "$PATHAUTOPREFIX/bin/ld"

# build cmake, needed to build LLVM
echo "building cmake"
cd "$PATHAUTOSRC"
[ -f "$VERSIONCMAKE.tar.gz" ] || run wget "https://cmake.org/files/$VERSIONCMAKEURL/$VERSIONCMAKE.tar.gz"
[ -d "$VERSIONCMAKE" ] || run tar xf "$VERSIONCMAKE.tar.gz"
mkdir -p "$PATHAUTOOBJ/$VERSIONCMAKE"
cd "$PATHAUTOOBJ/$VERSIONCMAKE"
[ -f Makefile ] || run "$PATHAUTOSRC/$VERSIONCMAKE/configure" --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install

# gperftools requires libunwind
echo "building libunwind"
cd "$PATHAUTOSRC"
[ -f "$VERSIONLIBUNWIND.tar.gz" ] || run wget "http://download.savannah.gnu.org/releases/libunwind/$VERSIONLIBUNWIND.tar.gz"
[ -d "$VERSIONLIBUNWIND" ] || run tar xf "$VERSIONLIBUNWIND.tar.gz"
run mkdir -p "$PATHAUTOOBJ/$VERSIONLIBUNWIND"
cd "$PATHAUTOOBJ/$VERSIONLIBUNWIND"
[ -f Makefile ] || run "$PATHAUTOSRC/$VERSIONLIBUNWIND/configure" --prefix="$PATHAUTOPREFIX"
run make -j"$JOBS"
run make install

# We need a patched LLVM
echo "building LLVM"
run mkdir -p "$PATHROOT/build"
cd "$PATHROOT/build"
[ -f Makefile ] || run cmake -DCMAKE_INSTALL_PREFIX="$PATHAUTOPREFIX" -DCMAKE_BUILD_TYPE=Debug -DLLVM_ENABLE_ASSERTIONS=ON -DLLVM_BUILD_TESTS=OFF -DLLVM_BUILD_EXAMPLES=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_TARGETS_TO_BUILD="X86;CppBackend" -DLLVM_BINUTILS_INCDIR="$PATHBINUTILS/include" -DCMAKE_C_FLAGS=-I"$PATHAUTOPREFIX/include" -DCMAKE_CXX_FLAGS=-I"$PATHAUTOPREFIX/include" -DCMAKE_C_FLAGS=-I"$PATHAUTOPREFIX/include" -DCMAKE_CXX_FLAGS=-I"$PATHAUTOPREFIX/include" -DCMAKE_EXE_LINKER_FLAGS=-L"$PATHAUTOPREFIX/lib" ../llvm
run make -j"$JOBS"
run make install

# Build baseline version of gperftools
echo "building gperftools"
cd "$PATHAUTOSRC"
if [ ! -d gperftools/.git ]; then
	run git clone https://github.com/gperftools/gperftools.git
	cd gperftools
	run git checkout "$VERSIONGPERFTOOLS"
fi
cd "$PATHAUTOSRC/gperftools"
if [ ! -f .autosetup.patched-gperftools-speedup ]; then
	run patch -p1 < "$PATHROOT/patches/GPERFTOOLS_SPEEDUP.patch"
	touch .autosetup.patched-gperftools-speedup
fi

[ -f configure ] || run autoreconf -fi
run mkdir -p "$PATHAUTOOBJ/gperftools"
cd "$PATHAUTOOBJ/gperftools"
[ -f Makefile ] || run "$PATHAUTOSRC/gperftools/configure" --prefix="$PATHAUTOPREFIXBASELINE"
run make -j"$JOBS"
run make install

echo "Fetching gperftools-metalloc"
cd "$PATHROOT"
if [ ! -d gperftools-metalloc/.git ]; then
        run git clone https://github.com/gperftools/gperftools.git gperftools-metalloc
        cd gperftools-metalloc
        run git checkout "$VERSIONGPERFTOOLS"
fi
cd "$PATHROOT/gperftools-metalloc"
if [ ! -f .autosetup.patched-gperftools-speedup ]; then
	run patch -p1 < "$PATHROOT/patches/GPERFTOOLS_SPEEDUP.patch"
	touch .autosetup.patched-gperftools-speedup
fi
if [ ! -f .autosetup.patched-gperftools-metalloc ]; then
	run patch -p1 < "$PATHROOT/patches/GPERFTOOLS_TYPESAN.patch"
	touch .autosetup.patched-gperftools-metalloc
fi

echo "building metapagetable"
cd "$PATHROOT/metapagetable"
export METALLOC_OPTIONS="-DFIXEDCOMPRESSION=$CONFIG_FIXEDCOMPRESSION -DMETADATABYTES=$CONFIG_METADATABYTES -DDEEPMETADATA=$CONFIG_DEEPMETADATA"
[ "true" = "$CONFIG_DEEPMETADATA" ] && METALLOC_OPTIONS="$METALLOC_OPTIONS -DDEEPMETADATABYTES=$CONFIG_DEEPMETADATABYTES"
rm -f metapagetable.h
run make config
run make -j"$JOBS"

# Build patched gperftools for new allocator
echo "building gperftools-metalloc"
cd "$PATHROOT/gperftools-metalloc"
[ -f configure ] || run autoreconf -fi
run mkdir -p "$PATHAUTOOBJ/gperftools-metalloc"
cd "$PATHAUTOOBJ/gperftools-metalloc"
[ -f Makefile ] || run "$PATHROOT/gperftools-metalloc/configure" --prefix="$PATHAUTOPREFIXTYPESAN"
run make -j"$JOBS"
run make install

if [ "$BUILD_SPEC" -ne 0 ]; then
	# build Perl, default perl does not work with perlbrew
	echo "building perl"
	cd "$PATHAUTOSRC"
	[ -f "$VERSIONPERL.tar.gz" ] || run wget "http://www.cpan.org/src/$VERSIONPERLURL/$VERSIONPERL.tar.gz"
	[ -d "$VERSIONPERL" ] || run tar xf "$VERSIONPERL.tar.gz"
	cd "$VERSIONPERL"
	PATHPERLPATCHES="$PATHROOT/patches"
	if [ ! -f .autosetup.patched-makedepend ]; then
		run patch -p1 < "$PATHPERLPATCHES/perl-makedepend.patch"
		touch .autosetup.patched-makedepend
	fi
	if [ ! -f .autosetup.patched-pagesize ]; then
		run patch -p1 < "$PATHPERLPATCHES/perl-pagesize.patch"
		touch .autosetup.patched-pagesize
	fi
	if [ ! -f .autosetup.patched-Configure ]; then
		libfile="`gcc -print-file-name=libm.so`"
		libpath="`dirname "$libfile"`"
		[ "$libpath" == . ] || sed -i "s|^xlibpth='|xlibpth='$libpath |" Configure
		touch .autosetup.patched-Configure
	fi
	[ -f Makefile ] || run "$PATHAUTOPREFIX/bin/bash" ./Configure -des -Dprefix="$PATHAUTOPREFIX"
	for m in makefile x2p/makefile; do
		grep -v "<command-line>" "$m" > "$m.tmp"
		mv "$m.tmp" "$m"
	done
	sed -i 's,# *include *<asm/page.h>,#define PAGE_SIZE 4096,' ext/IPC/SysV/SysV.xs
	run make -j"$JOBS"
	run make install

	# install perlbrew (needed by SPEC2006), fixing its installer in the process
	echo "building perlbrew"
	export PERLBREW_ROOT="$PATHAUTOSETUP/perl-root"
	export PERLBREW_HOME="$PATHAUTOSETUP/perl-home"
	run mkdir -p "$PATHAUTOSRC/perlbrew"
	cd "$PATHAUTOSRC/perlbrew"
	[ -f perlbrew-installer ] || run wget -O perlbrew-installer http://install.perlbrew.pl
	[ -f perlbrew-installer-patched ] || sed "s,/usr/bin/perl,$PATHAUTOPREFIX/bin/perl,g" perlbrew-installer > perlbrew-installer-patched
	run chmod u+x perlbrew-installer-patched
	run "$PATHAUTOPREFIX/bin/bash" ./perlbrew-installer-patched

	source "$PERLBREW_ROOT/etc/bashrc"
	export PATH="$PERLBREW_ROOT/perls/perl-5.8.8/bin:$PATH"

	echo "installing perl packages"
	if [ ! -f "$PATHAUTOSTATE/installed-perlbrew-perl-5.8.8" ]; then
		run perlbrew --notest install 5.8.8
		touch "$PATHAUTOSTATE/installed-perlbrew-perl-5.8.8"
	fi
	run perlbrew switch 5.8.8
	[ -f "$PERLBREW_ROOT/bin/cpanm" ] || run perlbrew install-cpanm
	run cpanm -n IO::Uncompress::Bunzip2
	run cpanm -n LWP::UserAgent
	run cpanm -n XML::SAX
	run cpanm -n IO::Scalar
	run cpanm -n Digest::MD5
fi

if [ "$BUILD_FIREFOX" -ne 0 ]; then
	echo "downloading Firefox"
	cd "$PATHAUTOSRC"
	[ -f "firefox-$VERSIONFIREFOX.source.tar.xz" ] || run wget "https://archive.mozilla.org/pub/firefox/releases/$VERSIONFIREFOX/source/firefox-$VERSIONFIREFOX.source.tar.xz"
	[ -d "firefox-$VERSIONFIREFOX" ] || run tar xf "firefox-$VERSIONFIREFOX.source.tar.xz"
	PATHFIREFOX="$PATHAUTOSRC/firefox-$VERSIONFIREFOX"
	cd "$PATHFIREFOX"
	if [ ! -f .autosetup.patched-NS_InvokeByIndex ]; then
		run patch -p0 < "$PATHROOT/patches/FIREFOX_NS_InvokeByIndex.patch"
		touch .autosetup.patched-NS_InvokeByIndex
	fi
fi

PATHSPECOUT=""
which prun > /dev/null && PATHSPECOUT="/local/$USER/cpu2006-output-root"

for instance in $INSTANCES; do
	instancename="$instance$INSTANCESUFFIX"
	cflags=""
	ldflagsalways=""
	ldflagsnolib=""
	ldflagslib=""
	prefix=""
	cc="$PATHAUTOPREFIX/bin/clang"
	cxx="$PATHAUTOPREFIX/bin/clang++"
	case "$instance" in
	baseline)
		prefix="$PATHAUTOPREFIXBASELINE"
		;;
	typesan*)
		cflags="$cflags -fsanitize=typesan"
		ldflagsalways="$ldflagsalways -fsanitize=typesan"
		prefix="$PATHAUTOPREFIXTYPESAN"
		;;
	esac
	if [ "$prefix" != "" ]; then
		ldflagsalways="$ldflagsalways -ltcmalloc -lpthread -lunwind"
		ldflagsalways="$ldflagsalways -L$prefix/lib -L$PATHAUTOPREFIX/lib"
		prefixbin="$prefix/bin"
		prefixlib="$prefix/lib"
	else
		prefixbin=""
		prefixlib=""
	fi
	ldflags="$ldflagsalways $ldflagsnolib"
	blacklist=""
	case "$instance" in
	typesanbl)
		blacklist="$PATHROOT/blacklist_stl.txt"
		;;
	typesanresid*)
		blacklist="$PATHROOT/blacklist_all.txt"
		;;
	esac
	cflagsff="$cflags"
	[ "$blacklist" = "" ] || cflags="$cflags -fsanitize-blacklist=$blacklist"

	echo "building ubench-$instancename"
	run make -C "$PATHROOT/ubench" SUFFIX="-$instancename" CXX="$cxx" CXXFLAGS="$cflags -O1" LDFLAGS="$ldflags" clean all
	(
		echo "#!$PATHAUTOPREFIX/bin/bash"
		echo "set -e"
		echo ""
		echo "export LD_LIBRARY_PATH=\"$prefixlib:$PATHAUTOPREFIX/lib:\$LD_LIBRARY_PATH\""
		echo "export PATH=\"$prefixbin:$PATHAUTOPREFIX/bin:\$PATH\""
		echo "cd \"$PATHROOT/ubench\""
		echo "taskset -c 1 ./ubench-$instancename"
	) > "$PATHROOT/run-ubench-$instancename.sh"
	chmod u+x "$PATHROOT/run-ubench-$instancename.sh"

	if [ "$BUILD_SPEC" -ne 0 ]; then
		echo "configuring SPEC2006-$instancename"
		for conftype in build run; do
		configname="TypeSan-$instancename-$conftype"
		(
			echo "tune        = base"
			echo "ext         = TypeSan-$instancename"
			echo "reportable  = no"
			echo "teeout      = yes"
			echo "teerunout   = no"
			echo "makeflags   = -j$JOBS"
			echo "strict_rundir_verify = no"
			[ "$conftype" = run -a -n "$PATHSPECOUT" ] && echo "output_root = $PATHSPECOUT"
			echo ""
			echo "default=default=default=default:"
			echo "CC          = $cc $cflags"
			echo "CXX         = $cxx $cflags"
			echo "FC          = `which false`"
			echo "COPTIMIZE   = -O2 -fno-strict-aliasing -std=gnu89"
			echo "CXXOPTIMIZE = -O2 -fno-strict-aliasing"
			echo "CLD         = $cc $ldflags"
			echo "CXXLD       = $cxx $ldflags"
			echo ""
			echo "default=base=default=default:"
			echo "PORTABILITY    = -DSPEC_CPU_LP64"
			echo ""
			echo "400.perlbench=default=default=default:"
			echo "CPORTABILITY   = -DSPEC_CPU_LINUX_X64"
			echo ""
			echo "462.libquantum=default=default=default:"
			echo "CPORTABILITY   =  -DSPEC_CPU_LINUX"
			echo ""
			echo "483.xalancbmk=default=default=default:"
			echo "CXXPORTABILITY = -DSPEC_CPU_LINUX"
			echo ""
			echo "481.wrf=default=default=default:"
			echo "wrf_data_header_size = 8"
			echo "CPORTABILITY   = -DSPEC_CPU_CASE_FLAG -DSPEC_CPU_LINUX"
		) > "$PATHSPEC/config/$configname.cfg"
		
		if [ "$conftype" = build ]; then
			scriptpath="$PATHSPEC/$conftype-spec-$instancename.sh"
		else
			scriptpath="$PATHROOT/$conftype-spec-$instancename.sh"
		fi
		(
			echo "#!/bin/bash"
			echo "set -e"
			echo ""
			echo "export LD_LIBRARY_PATH=\"$prefixlib:$PATHAUTOPREFIX/lib:\$LD_LIBRARY_PATH\""
			echo "export PATH=\"$prefixbin:$PATHAUTOPREFIX/bin:\$PATH\""
			echo "export PERLBREW_HOME=\"$PERLBREW_HOME\""
			echo "export PERLBREW_ROOT=\"$PERLBREW_ROOT\""
			echo "cd \"$PATHSPEC\""
			echo "source \"$PERLBREW_ROOT/etc/bashrc\""
			echo "source \"$PATHSPEC/shrc\""
			if [ "$conftype" = run -a -n "$PATHSPECOUT" ]; then
				echo "rm -rf \"$PATHSPECOUT\""
				echo "mkdir -p \"$PATHSPECOUT\""
				echo "mkdir -p \"$PATHSPEC/result\""
				echo "ln -s \"$PATHSPEC/result\" \"$PATHSPECOUT\""
				echo "for arg in \"\$@\"; do"
				echo "  if [ -d \"$PATHSPEC/benchspec/CPU2006/\$arg/exe\" ]; then"
				echo "    mkdir -p \"$PATHSPECOUT/benchspec/CPU2006/\$arg\""
				echo "    cp -r \"$PATHSPEC/benchspec/CPU2006/\$arg/exe\" \"$PATHSPECOUT/benchspec/CPU2006/\$arg\""
				echo "  fi"
				echo "done"
			fi
			[ "$conftype" = run ] && echo -n "taskset -c 0 "
			echo -n "runspec --config=\"$configname\" \"\$@\""
			[ "$conftype" = build ] && echo -n " --action=build"
			[ "$conftype" = run ] && echo -n " --nobuild"
			if [ "$conftype" = run -a -n "$PATHSPECOUT" ]; then
				echo " | sed 's,$PATHSPECOUT/result/,$PATHSPEC/result/,g'"
				echo "cp \`find \"$PATHSPECOUT\" -name simpleprof.*.txt\` \"$PATHSPEC/result\" || true"
				echo "rm -rf \"$PATHSPECOUT\""
			else
				echo ""
			fi
		) > "$scriptpath"
		run chmod u+x "$scriptpath"
		done
	fi

	if [ "$BUILD_FIREFOX" -ne 0 ]; then
		echo "configuring Firefox-$instancename"
		pathobj="$PATHFIREFOX/obj-TypeSan-$instancename"
		pathbinwrap="$pathobj/binwrap"
		run mkdir -p "$pathbinwrap"

		buildffclangwrapper()
		{
			echo "#!/bin/bash"
			echo "set -e"
			echo "XARGS=\"$ldflags -lunwind -lstdc++ -lm\""
			echo "for arg in \"\$@\"; do"
			echo "  case \"\$arg\" in"
			echo "  -c|-E|-V)"
			echo "    XARGS=\"\""
			echo "    ;;"
			echo "  -shared)"
			echo "    XARGS=\"$ldflagsalways $ldflagslib\""
			echo "    ;;"
			echo "  esac"
			echo "done"
			echo "$PATHAUTOPREFIX/bin/$1 \"\$@\" \$XARGS"
		}
		buildffclangwrapper clang > "$pathbinwrap/clang"
		run chmod u+x "$pathbinwrap/clang"
		buildffclangwrapper clang++ > "$pathbinwrap/clang++"
		run chmod u+x "$pathbinwrap/clang++"

		buildbinutilswrapper()
		{
			echo "#!/bin/bash"
			echo "set -e"
			echo "$PATHAUTOPREFIX/bin/$1 \"\$@\" --plugin $PATHAUTOPREFIX/lib/LLVMgold.so"
		}
		buildbinutilswrapper ar > "$pathbinwrap/ar"
		run chmod u+x "$pathbinwrap/ar"
		buildbinutilswrapper nm > "$pathbinwrap/nm"
		run chmod u+x "$pathbinwrap/nm"
		buildbinutilswrapper ranlib > "$pathbinwrap/ranlib"
		run chmod u+x "$pathbinwrap/ranlib"

		(
			[ "$blacklist" = "" ] || cat "$blacklist"

			# AlignedStorage2<T> is a problem; it allocates the requested
			# object as a byte array in a union and is safe to cast to that
			# object but there is no way for us to know that. It shouldn't
			# really directly be typecast though because it provides
			# operators to handle that safely. We don't want to blanket
			# blacklist it as it is widely used and could potentially result
			# in unsafe casts. Best solution would be to remove any explicit
			# AlignedStorage2<T> casts in Firefox.
			echo "fun:_Z13AsFixedStringPK18nsAString_internal" # nsFixedString allocated through mozilla::AlignedStorage2<nsAutoString>
			echo "fun:_ZN14nsStringBuffer10FromStringERK18nsAString_internal" # nsAStringAccessor allocated through mozilla::AlignedStorage2<T>
			echo "fun:_ZN14nsStringBuffer8ToStringEjR18nsAString_internalb" # nsAStringAccessor allocated through mozilla::AlignedStorage2<T>
			echo "fun:_ZNK2js3jit12RInstruction13toResumePointEv" # RResumePoint allocated through mozilla::AlignedStorage<16ul>
			echo "fun:_ZN2JS12AutoGCRooter5traceEP8JSTracer" # JS::CustomAutoRooter allocated through mozilla::AlignedStorage2<js::AutoRooterGetterSetter::Inner>
			echo "fun:_ZN2js8frontend10MarkParserEP8JSTracerPN2JS12AutoGCRooterE" # casts mozilla::AlignedStorage2<js::frontend::Parser<js::frontend::FullParseHandler> > and mozilla::AlignedStorage2<js::frontend::Parser<js::frontend::SyntaxParseHandler> > to js::frontend::Parser<js::frontend::FullParseHandler>

			# types with false positives
			#echo "type:_ZTIN2JS6RootedINS_5ValueEEE" # JS::Rooted<JS::Value> descends from (Mutable)ValueOperations but is cast to JS::Value in js::ValueOperations<JS::Rooted<JS::Value> >::value()
			echo "type:_ZTI7BLK_HDR" # BLK_HDR is used to allocate space for other objects
			echo "type:_ZTIN2js17FakeMutableHandleIN2JS5ValueEEE" # js::FakeMutableHandle<JS::Value> is cast to JS::MutableHandle<JS::Value> in js::(Mutable)ValueOperations<JS::MutableHandle<JS::Value> >::value()
			echo "type:_ZTI16PatternFromState" # cast to ColorPattern because it starts with a union containing that type
			echo "type:_ZTIN7mozilla3gfx14GeneralPatternE" # cast to mozilla::gfx::LinearGradientPattern because it starts with a union containing that type
			echo "type:_ZTIN7mozilla6layers15LayerAttributesE" # contains problematic union mozilla::layers::SpecificLayerAttributes::Value
			echo "type:_ZTIN7mozilla6layers23SpecificLayerAttributesE" # contains problematic union mozilla::layers::SpecificLayerAttributes::Value
			echo "type:_ZTIN7mozilla6layers23SpecificLayerAttributes5ValueE" # problematic union
			
			# function with crash
			echo "fun:_ZN20BaselineStackBuilderC2ERN2js3jit16JitFrameIteratorEm" # cast of stack pointer
			
			# functions with false positives
			echo "fun:_ZN2js10InlineListINS_3jit12MoveResolver11PendingMoveEE6removeEPNS_14InlineListNodeIS3_EE" # node is allocated through pool allocator in MoveResolver::addMove
			echo "fun:_ZNK2js10InlineListINS_3jit12MoveResolver11PendingMoveEE5beginEv" # node is allocated through pool allocator in MoveResolver::addMove
			echo "fun:_ZN2js18InlineListIteratorINS_3jit12MoveResolver11PendingMoveEEppEv" # node is allocated through pool allocator in MoveResolver::addMove

			# not properly debugged
			echo "fun:_ZNK2js15ValueOperationsIN2JS6RootedINS1_5ValueEEEE5valueEv"
			echo "fun:_ZN2js22MutableValueOperationsIN2JS6RootedINS1_5ValueEEEE5valueEv"
			echo "fun:_ZNK7mozilla3gfx13BaseIntRegionINS0_14IntRegionTypedINS0_12UnknownUnitsEEENS0_12IntRectTypedIS3_EENS0_13IntPointTypedIS3_EENS0_14IntMarginTypedIS3_EEE4ThisEv"
			echo "fun:_ZN7mozilla3gfx13BaseIntRegionINS0_14IntRegionTypedINS0_12UnknownUnitsEEENS0_12IntRectTypedIS3_EENS0_13IntPointTypedIS3_EENS0_14IntMarginTypedIS3_EEE4ThisEv"
			echo "fun:_ZN7mozilla3gfx13BaseIntRegionINS0_14IntRegionTypedINS_10LayerPixelEEENS0_12IntRectTypedIS3_EENS0_13IntPointTypedIS3_EENS0_14IntMarginTypedIS3_EEE4ThisEv"
			echo "fun:_ZN19nsXPCWrappedJSClass10CallMethodEP14nsXPCWrappedJStPK19XPTMethodDescriptorP17nsXPTCMiniVariant"
			for op in EpLE EmIE EmLE; do
				echo "fun:_ZN7mozilla3gfx9BaseCoordI*$op*"
				echo "fun:_ZN7mozilla3gfx10BaseMarginI*$op*"
				echo "fun:_ZN7mozilla3gfx9BasePointI*$op*"
				echo "fun:_ZN7mozilla3gfx8BaseRectI*$op*"
				echo "fun:_ZN7mozilla3gfx8BaseSizeI*$op*"
			done
		) > "$PATHFIREFOX/.blacklist-TypeSan-$instancename"
		cflagsff="$cflagsff -fsanitize-blacklist=$PATHFIREFOX/.blacklist-TypeSan-$instancename"

		configpath="$PATHFIREFOX/.mozconfig-TypeSan-$instancename"
		(
			echo "export CC=\"$pathbinwrap/clang $cflags\""
			echo "export CXX=\"$pathbinwrap/clang++ $cflags\""
			echo "export CFLAGS=\"$cflagsff\""
			echo "export CXXFLAGS=\"$cflagsff\""
			echo "export LDFLAGS=\"$cflagsff\""
			echo "ac_add_options --disable-jemalloc"
			echo "ac_add_options --disable-crashreporter"
			echo "ac_add_options --disable-elf-hack"
			echo "ac_add_options --disable-debug-symbols"
			echo "ac_add_options --disable-install-strip"
			echo "ac_add_options --disable-debug"
			echo "ac_add_options --disable-tests"
			echo "ac_add_options --enable-llvm-hacks"
			echo "mk_add_options MOZ_OBJDIR=\"$pathobj\""
			echo "mk_add_options MOZ_MAKE_FLAGS=\"-j$JOBS\""
		) > "$configpath"
		(
			echo "#!/bin/bash"
			echo "set -e"
			echo "export PATH=\"$pathbinwrap:$PATHAUTOPREFIX/bin:\$PATH\""
			echo "export LD_LIBRARY_PATH=\"$prefixlib:$PATHAUTOPREFIX/lib:\$LD_LIBRARY_PATH\""
			echo "export MOZCONFIG=\"$configpath\""
			echo "cd \"$PATHFIREFOX\""
			echo "make -k -f client.mk || true # somehow the first build fails sometimes, maybe a missing dependency in their build system?"
			echo "make -f client.mk"
		) > "$PATHFIREFOX/build-TypeSan-$instancename.sh"
		run chmod u+x "$PATHFIREFOX/build-TypeSan-$instancename.sh"

		(
			echo "#!/bin/bash"
			echo "set -e"
			echo "export LD_LIBRARY_PATH=\"$prefixlib:$PATHAUTOPREFIX/lib:\$LD_LIBRARY_PATH\""
			echo "\"$pathobj/dist/bin/firefox\" \"\$@\""
		) > "$PATHROOT/run-firefox-$instancename.sh"
		run chmod u+x "$PATHROOT/run-firefox-$instancename.sh"
	fi
done

if [ "$BUILD_SPEC" -ne 0 ]; then
	if [ "$CLEAN_SPEC" -ne 0 ]; then
		find "$PATHSPEC/benchspec/CPU2006" -name build -type d | xargs rm -rf
		find "$PATHSPEC/benchspec/CPU2006" -name exe   -type d | xargs rm -rf
		find "$PATHSPEC/benchspec/CPU2006" -name run   -type d | xargs rm -rf
	fi
	
	for instance in $INSTANCES; do
		instancename="$instance$INSTANCESUFFIX"
		if [ "$instance" = typesanresidno ]; then
			export TYPECHECK_DISABLE_STACK_OPT=1
		else
			unset TYPECHECK_DISABLE_STACK_OPT
		fi
		for benchmark in  $BENCHMARKS; do
			echo "building SPEC2006-$instancename $benchmark"
			logsuffix="spec-$instancename-$benchmark" run time "$PATHSPEC/build-spec-$instancename.sh" "$benchmark"
		done
	
		echo "updating SPEC2006-$instancename MD5s"
		configbuild="$PATHSPEC/config/TypeSan-$instancename-build.cfg"
		configrun="$PATHSPEC/config/TypeSan-$instancename-run.cfg"
		lineno="`grep -n __MD5__ "$configbuild" | cut -d: -f1`"
		tail -n "+$lineno" "$configbuild" >> "$configrun"
	done
fi

if [ "$BUILD_FIREFOX" -ne 0 ]; then
	for instance in $INSTANCES; do
		instancename="$instance$INSTANCESUFFIX"
		echo "building Firefox-$instancename"
		logsuffix="firefox-$instancename" run time "$PATHFIREFOX/build-TypeSan-$instancename.sh"
	done
fi

echo done

