#!/bin/bash

ssv_phy=""
phy_dirs="/sys/class/ieee80211/*"
SSV_CMD_FILE=""

for phy_dir in $phy_dirs; do
    if [ ! -d ${phy_dir}/device/driver ]; then
		exit 1;
	fi
	drv_name=`ls ${phy_dir}/device/driver | grep SV6`

    if [ ${drv_name} ]; then
    	ssv_phy=`basename $phy_dir`;
    	break;
    fi
done

if [ ${ssv_phy} ]; then
	SSV_CMD_FILE=/proc/ssv/${ssv_phy}/ssv_cmd
else 
	echo "Cannot find SSV CLI"
    exit 1
fi

if [[ ${1} == "tx" ]]; then
    # Set block mode
    echo "rf block" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE
    # Set default channel
    echo "ch 1" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE
    # Set default rate
    echo "rf set rate 22" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE
    # txgen
    echo "rf phy_txgen 0xffffffff" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE

elif [[ ${1} == "rx" ]]; then
    # Set block mode
    echo "rf block" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE
    # disable ack policy
    echo "rf set ack disable" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE
    # Set default channel
    echo "ch 1" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE
    # Reset MIB
    echo "mib reset" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE

elif [[ ${1} == "stop" ]]; then
    echo "rf unblock" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE

elif [[ ${1} == "reset" ]]; then
    echo "mib reset" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE

elif [[ ${1} == "count0" ]]; then
    echo "rf count 0" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE

elif [[ ${1} == "count1" ]]; then
    echo "rf count 1" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE

elif [[ ${1} == "cli" ]]; then
    shift
    echo "$*" > $SSV_CMD_FILE
    cat $SSV_CMD_FILE
else
	echo "rf_tool.sh [tx|rx|stop|reset|cli]"
fi

