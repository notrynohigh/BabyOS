#!/bin/bash

bos_path="../../../bos"

echo bos path : $bos_path

python -m pip install --upgrade pip

pip install kconfiglib

python b_config.py $bos_path

if [ $? -eq 0 ]; then
    echo ""
else
    python3 b_config.py $bos_path
fi

