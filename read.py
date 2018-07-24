#! /usr/bin/python3
import time

card_none = '0000000000'
filename = '/sys/kernel/wiegand_reader/cardnum'

with open(filename, 'r') as f:
    card_num_prev = None
    while True:
        s = f.read()
        if card_none in s:
            card_num_prev = None
        else:
            _short, _long = s.split(' ')
            card_num = _long.split(':')   
            if card_num!=card_num_prev:
                print(s)
            card_num_prev = card_num                    

        time.sleep(0.1)
        f.seek(0)


