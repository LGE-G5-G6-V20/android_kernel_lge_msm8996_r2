#!/bin/bash
#
## BUILD SCRIPT TO AUTOMATE SWAN2000 BUILDS FOR ALL SUPPORTED DEVICES ##
#
# Before you start, download a working toolchain for aarch64 and arm32
# if you don't have any and extract them to the following directory:
# $HOME/toolchains/{aarch64-elf , arm-eabi}
#
# And only then run this script to build for all supported devices,
# or else it will fail due to not finding any suitable toolchains.
#
################### MODELS THIS SCRIPT WILL BUILD FOR ###################
#
# LG G5:
# H830		= T-Mobile (US)
# H850		= International (Global)
# RS988		= Unlocked (US)
#
#  ---------------------------------------
# LG V20:
# H910		= AT&T (US)
# H918		= T-Mobile (US)
# US996		= US Cellular & Unlocked (US) (Officially unlocked)
# US996D	= US Cellular & Unlocked (US) (Unlocked with Dirtysanta)
# VS995		= Verizon (US)
# H990DS	= International (Global) (Uses common H990 kernel)
# H990TR	= Turkey (TR) (Uses common H990 kernel)
# LS997		= Sprint (US)
#
#  ---------------------------------------
# LG G6:
# H870		= International (Global)
# US997		= US Cellular & Unlocked (US)
# H872		= T-Mobile (US)

# color codes
COLOR_N="\033[0m"
COLOR_R="\033[0;31m"
COLOR_G="\033[1;32m"
COLOR_Y="\033[1;33m"
COLOR_B="\033[1;34m"
COLOR_P="\033[1;35m"

# Array of supported models
MODEL_ARRAY=("H850" "H830" "RS988" "H870" "US997" "H872" "H910" "H918" "H990" "LS997" "US996" "US996D" "VS995")

WILL_BUILD="no"

BUILD_ALL () {
    echo -e $COLOR_G"This script is used solely to automate builds of: \n"$COLOR_B
    echo -e "   ______       _____    _   __ ___   __  __  __  "
    echo -e "  / ___/ |     / /   |  / | / /|__ \ /__\/__\/__\ "
    echo -e "  \__ \| | /| / / /| | /  |/ / __/ /// /// /// // "
    echo -e " ___/ /| |/ |/ / ___ |/ /|  / / __///_///_///_//  "
    echo -e "/____/ |__/|__/_/  |_/_/ |_/ /____/\__/\__/\__/   "
    echo -e "         Developers: stendro + AShiningRay        "
    echo -e $COLOR_P"\n\nYOU DON'T NEED THIS TO BUILD FOR A SINGLE DEVICE!\n"
    echo -e $COLOR_Y"Do you wish to build for all supported devices?"
    
    read -p "[yes, anything_else_thats_not_yes] -> " WILL_BUILD

    if [ $WILL_BUILD = "yes" ]; then
        echo -e $COLOR_B"\nvvv Beginning build for all supported devices! vvv\n"$COLOR_N

        for DEVICE in "${MODEL_ARRAY[@]}"; do
            echo -e $COLOR_B"|----------------------${DEVICE}----------------------|\n"$COLOR_N
            ./build.sh $DEVICE "build_all"

            echo -e $COLOR_B"\nPacking up ${DEVICE}'s kernel..."$COLOR_N
            ./copy_finished.sh "build_all"

            echo -e $COLOR_B"\n${DEVICE} kernel is ready! \n"$COLOR_N
        done
        echo -e $COLOR_B"\nSwan2000 was built for all supported devices! \n --- Exiting...\n"$COLOR_N
    else
        echo -e "Build aborted!" $COLOR_N
    fi
}

BUILD_ALL
