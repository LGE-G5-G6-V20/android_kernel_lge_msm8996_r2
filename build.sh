#!/bin/bash
#
# Stock kernel for LG Electronics msm8996 devices build script by jcadduono
# -(heavily)modified by stendro
#
############################# BEFORE STARTING #############################
#
# download a working toolchain and extract it somewhere and configure this
# file to point to the toolchain's root directory.
#
# once you've set up the config section how you like it, you can simply run
# ./build.sh [VARIANT]
#
################################ VARIANTS ################################
#
# H850		= International (Global)
#		LGH850   (LG G5)
#
# H830		= T-Mobile (US)
#		LGH830   (LG G5)
#
# RS988		= Unlocked (US)
#		LGRS988  (LG G5)
#
#   *************************
#
# H910		= AT&T (US)
#		LGH910   (LG V20)
#
# H915		= Canada (CA)
#		LGH915   (LG V20)
#
# H918		= T-Mobile (US)
#		LGH918   (LG V20)
#
# US996		= US Cellular & Unlocked (US)
#		LGUS996  (LG V20)
#
# US996D	= US Cellular & Unlocked (US)
#		LGUS996  (LG V20) (Unlocked with Engineering Bootloader)
#
# VS995		= Verizon (US)
#		LGVS995  (LG V20)
#
# H990DS	= International (Global)
#		LGH990   (LG V20)
#
# H990TR	= Turkey (TR)
#		LGH990   (LG V20)
#
# LS997		= Sprint (US)
#		LGLS997  (LG V20)
#
# F800K/L/S	= Korea (KR)
#		LGF800   (LG V20)
#
#   *************************
#
# H870		= International (Global)
#		LGH870   (LG G6)
#
# US997		= US Cellular & Unlocked (US)
#		US997    (LG G6)
#
# H872		= T-Mobile (US)
#		LGH872   (LG G6)
#
################################# CONFIG #################################

# Assume build_all is not being used, will be automatically changed if it is
SINGLEBUILD="yes"

# root directory of this kernel (this script's location)
RDIR=$(pwd)

# build dir
BDIR=build

# version file
VFIL=VERSION

# expand version
VER=$(cat $RDIR/$VFIL)

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# ------------------------- BUILD CONFIG OPTIONS -------------------------
#
# "user"@"host"
KBUSER=stendro_+_AShiningRay
KBHOST=github

# ccache: yes or no
USE_CCACHE=no

# select cpu threads
THREADS=$(grep -c "processor" /proc/cpuinfo)

# directory containing cross-compiler
# a newer toolchain (gcc8+) is recommended due to changes made
# to the kernel.
GCC_COMP=$HOME/toolchains/aarch64-elf/bin/aarch64-elf-
# directory containing 32bit cross-compiler for CONFIG_COMPAT_VDSO
GCC_COMP_32=$HOME/toolchains/arm-eabi/bin/arm-eabi-

# -------------------------------- END -----------------------------------
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# compiler version
# gnu gcc or newer arm (linaro) gcc
if $(${GCC_COMP}gcc --version | grep -q '(GCC)') || 
$(${GCC_COMP}gcc --version | grep -q '(Eva GCC)'); then
	GCC_STRING=$(${GCC_COMP}gcc --version | head -n1 | cut -f2 -d')')
	GCC_VER="GCC$GCC_STRING"
else # old linaro gcc
	GCC_VER="$(${GCC_COMP}gcc --version | head -n1 | cut -f1 -d')' | \
	cut -f2 -d'(')"
	
	if $(echo $GCC_VER | grep -q '~dev'); then
  		GCC_VER="$(echo $GCC_VER | cut -f1 -d'~')+"
fi
fi

# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# --------------------------- RIGID PORTION ------------------------------
#
# color codes
COLOR_N="\033[0m"
COLOR_R="\033[0;31m"
COLOR_G="\033[1;32m"
COLOR_Y="\033[1;33m"
COLOR_P="\033[1;35m"

ABORT() {
	echo -e $COLOR_R"Error: $*"
	exit 1
}

export ARCH=arm64
export KBUILD_BUILD_USER=$KBUSER
export KBUILD_BUILD_HOST=$KBHOST
export LOCALVERSION="-${VER}"
if [ "$USE_CCACHE" = "yes" ]; then
  export CROSS_COMPILE="ccache $GCC_COMP"
  export CROSS_COMPILE_ARM32="ccache $GCC_COMP_32"
else
  export CROSS_COMPILE=$GCC_COMP
  export CROSS_COMPILE_ARM32=$GCC_COMP_32
fi

# In case a model isn't passed as an argument, this block acts as a fallback
MODEL_ARRAY=("H850" "H830" "RS988" "H870" "US997" "H872" "H910" "H918" "H990" "LS997" "US996" "US996D" "VS995")
FALLBACK_GET_VARIANT() {
	if [[ ${SELECTED_MODEL} = "" ]]; then
		echo -e "List of available variants:"
		echo -e "G5  -> [$COLOR_C H850, H830, RS988 $COLOR_N]"
		echo -e "G6  -> [$COLOR_C H870, US997, H872 $COLOR_N]"
		echo -e "V20 -> [$COLOR_C H910, H918, H990, LS997, US996, US996D (Dirtysanta), VS995 $COLOR_N]"
		read -p "Please select your model:" DEVICE
	fi

	# This checks if the user's model is supported by the kernel.
	if [[ " ${MODEL_ARRAY[*]} " != *" ${DEVICE} "* ]];	then
		echo -e "${COLOR_R}Your model wasn't found. Please check for errors (such as lower-case).${COLOR_N} \n"
		FALLBACK_GET_VARIANT
	fi
}

# selected device
[ "$1" ] && DEVICE=$1
[ "$DEVICE" ] || FALLBACK_GET_VARIANT

# Checks if the build_all script isn't being used
if [ "$2" = "build_all" ]; then
    SINGLEBUILD="no"
else
    SINGLEBUILD="yes"
fi

# link device name to lg config files
if [ "$DEVICE" = "H850" ]; then
  DEVICE_DEFCONFIG=lineageos_h850_defconfig
elif [ "$DEVICE" = "H830" ]; then
  DEVICE_DEFCONFIG=lineageos_h830_defconfig
elif [ "$DEVICE" = "RS988" ]; then
  DEVICE_DEFCONFIG=lineageos_rs988_defconfig
elif [ "$DEVICE" = "H870" ]; then
  DEVICE_DEFCONFIG=lineageos_h870_defconfig
elif [ "$DEVICE" = "H872" ]; then
  DEVICE_DEFCONFIG=lineageos_h872_defconfig
elif [ "$DEVICE" = "US997" ]; then
  DEVICE_DEFCONFIG=lineageos_us997_defconfig
elif [ "$DEVICE" = "H918" ]; then
  DEVICE_DEFCONFIG=lineageos_h918_defconfig
elif [ "$DEVICE" = "H910" ]; then
  DEVICE_DEFCONFIG=lineageos_h910_defconfig
elif [ "$DEVICE" = "H990" ]; then
  DEVICE_DEFCONFIG=lineageos_h990_defconfig
elif [ "$DEVICE" = "US996" ]; then
  DEVICE_DEFCONFIG=lineageos_us996_defconfig
elif [ "$DEVICE" = "US996D" ]; then
  DEVICE_DEFCONFIG=lineageos_us996d_defconfig
elif [ "$DEVICE" = "VS995" ]; then
  DEVICE_DEFCONFIG=lineageos_vs995_defconfig
elif [ "$DEVICE" = "LS997" ]; then
  DEVICE_DEFCONFIG=lineageos_ls997_defconfig
else
  ABORT "Invalid device '${DEVICE}' specified! Make sure to use upper-case."
fi

# check for stuff
[ -f "$RDIR/arch/$ARCH/configs/${DEVICE_DEFCONFIG}" ] \
	|| ABORT "$DEVICE_DEFCONFIG not found in $ARCH configs!"

[ -x "${GCC_COMP}gcc" ] \
	|| ABORT "Cross-compiler not found at: ${GCC_COMP}gcc"

[ -x "${GCC_COMP_32}gcc" ] \
	|| echo -e $COLOR_R"32-bit compiler not found, required for COMPAT_VDSO (VDSO32)."

if [ "$USE_CCACHE" = "yes" ]; then
	command -v ccache >/dev/null 2>&1 \
	|| ABORT "Do you have ccache installed?"
fi

if [ -f "$BDIR/DEVICE" ] && \
	[ "$(cat $BDIR/DEVICE)" = "$DEVICE" ]; then
	ASK_CLEAN=yes
fi

# build commands
CLEAN_BUILD() {
	echo -e $COLOR_G"Cleaning build folder..."$COLOR_N
	rm -rf $BDIR && sleep 5
}

SETUP_BUILD() {
	echo -e $COLOR_G"Creating kernel config..."$COLOR_N
	mkdir -p $BDIR
	echo "$DEVICE" > $BDIR/DEVICE \
		|| echo -e $COLOR_R"Failed to reflect device!"
    if [ $SINGLEBUILD = "yes" ]; then
	    make -C "$RDIR" O=$BDIR "$DEVICE_DEFCONFIG" \
		    || ABORT "Failed to set up the kernel build."
    else # build_all will send make output to a file
        make -C "$RDIR" O=$BDIR "$DEVICE_DEFCONFIG" &> zBuild_all.log \
		    || ABORT "Failed to set up the kernel build."
    fi
}

BUILD_KERNEL() {
	    echo -e $COLOR_G"Compiling kernel for ${DEVICE}..."$COLOR_N
	    TIMESTAMP1=$(date +%s)
    if [ $SINGLEBUILD = "yes" ]; then
        while ! make -C "$RDIR" O=$BDIR -j"$THREADS"; do
		    read -rp "Build failed. Retry? " do_retry
		    case $do_retry in
			    Y|y) continue ;;
			    *) ABORT "Compilation aborted." ;;
		    esac
	    done
    else # build_all will send compile logs to a file
	    while ! make -C "$RDIR" O=$BDIR -j"$THREADS" &> zBuild_all.log; do
		    read -rp "Build failed. Retry? " do_retry
		    case $do_retry in
			    Y|y) continue ;;
			    *) ABORT "Compilation aborted." ;;
		    esac
	    done
    fi
	    TIMESTAMP2=$(date +%s)
	    BSEC=$((TIMESTAMP2-TIMESTAMP1))
	    BTIME=$(printf '%02dm:%02ds' $(($BSEC/60)) $(($BSEC%60)))
}

INSTALL_MODULES() {
	grep -q 'CONFIG_MODULES=y' $BDIR/.config || return 0
	echo -e $COLOR_G"Installing kernel modules..."$COLOR_N
    if [ $SINGLEBUILD = "yes" ]; then
        make -C "$RDIR" O=$BDIR \
	        INSTALL_MOD_PATH="." \
	        INSTALL_MOD_STRIP=1 \
	        modules_install
    else # build_all will send module logs to a file
        make -C "$RDIR" O=$BDIR \
            INSTALL_MOD_PATH="." \
            INSTALL_MOD_STRIP=1 \
            modules_install &> zBuild_all.log
    fi
	rm $BDIR/lib/modules/*/build $BDIR/lib/modules/*/source
}

PREPARE_NEXT() {
	if grep -q 'CONFIG_KERNEL_LZ4=y' $BDIR/.config; then
	  echo lz4 > $BDIR/COMPRESSION \
		|| echo -e $COLOR_R"Failed to reflect compression method!"
	else
	  echo gz > $BDIR/COMPRESSION \
		|| echo -e $COLOR_R"Failed to reflect compression method!"
	fi
	git log --oneline -50 > $BDIR/GITCOMMITS \
		|| echo -e $COLOR_R"Failed to reflect commit log!"
}

cd "$RDIR" || ABORT "Failed to enter $RDIR!"
echo -e $COLOR_G"Building ${DEVICE} ${VER}..."
echo -e $COLOR_P"Using $GCC_VER..."
if [ "$USE_CCACHE" = "yes" ]; then
  echo -e $COLOR_P"Using CCACHE..."
fi

# ask before cleaning if device
# is the same as previous build
if [ $SINGLEBUILD = "yes" ]; then
    if [ "$ASK_CLEAN" = "yes" ]; then
      while true; do
        echo -e $COLOR_Y
        read -p "Same device as the last build. Do you wish to clean the build directory?" yn
        echo -e $COLOR_N
        case $yn in
          [Yy]* ) CLEAN_BUILD && break ;;
          [Nn]* ) break ;;
          * ) echo -e $COLOR_R"Please answer 'y' or 'n'"$COLOR_N ;;
        esac
      done
    else
    CLEAN_BUILD
    fi
else # Always clean build folder for next build on build_all
    CLEAN_BUILD
fi
SETUP_BUILD
BUILD_KERNEL
INSTALL_MODULES
PREPARE_NEXT
echo -e $COLOR_G"Finished building ${DEVICE} ${VER} -- Kernel compilation took"$COLOR_R $BTIME

if [ $SINGLEBUILD = "yes" ]; then
    echo -e $COLOR_P"Run './copy_finished.sh' to create the flashable AnyKernel zip."
fi
