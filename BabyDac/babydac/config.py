
# -*- coding: utf-8 -*-
"""
Created on Mon Mar 23 09:51:18 2026

@author: UmeshPuri
"""
InstrumentData = {
    "B_DAC": {
        "mains_frequency":50,
        "log_sweep":[1e-6,1e-5,1e-4,1e-3,1e-2,1e-1,1.00],
        "baud_rate": 115200,
        "LOGICAL_CHANNEL_MIN": 0,
        "LOGICAL_CHANNEL_MAX":23,
        "TGP_MIN":0,
        "TGP_MAX":5,
        "TRIG_MIN":0,
        "TRIG_MAX":1,
        "TRIG_V_MIN":-10.0,
        "TRIG_V_MAX":10.0,
        "SEQ_TRIG_MIN":0,
        "SEQ_TRIG_MAX":3,
        "CHAIN_NONE":0xFF,
        "MAX_PACKET_BYTES":2048
    },
}

BoardData = {

 "B_DAC": {
    "dac_channels": {
      "type": "B_DAC_Channel",
      "pins": {
        "dac_01": {
          "type": "B_DAC_Channel",
          "i_o": "B1_1",
          "port": 0
        },
        "dac_02": {
          "type": "B_DAC_Channel",
          "i_o": "B1_2",
          "port": 1
        },
        "dac_03": {
          "type": "B_DAC_Channel",
          "i_o": "B1_3",
          "port": 2
        },
        "dac_04": {
          "type": "B_DAC_Channel",
          "i_o": "B1_4",
          "port": 3
        },
        "dac_05": {
          "type": "B_DAC_Channel",
          "i_o": "B1_5",
          "port": 4
        },
        "dac_06": {
          "type": "B_DAC_Channel",
          "i_o": "B1_6",
          "port": 5
        },
        "dac_07": {
          "type": "B_DAC_Channel",
          "i_o": "B1_7",
          "port": 6
        },
        "dac_08": {
          "type": "B_DAC_Channel",
          "i_o": "B1_8",
          "port": 7
        },
        "dac_09": {
          "type": "B_DAC_Channel",
          "i_o": "B1_9",
          "port": 8
        },
        "dac_10": {
          "type": "B_DAC_Channel",
          "i_o": "B1_10",
          "port": 9
        },
        "dac_11": {
          "type": "B_DAC_Channel",
          "i_o": "B1_11",
          "port": 10
        },
        "dac_12": {
          "type": "B_DAC_Channel",
          "i_o": "B1_12",
          "port": 11
        },
        "dac_13": {
          "type": "B_DAC_Channel",
          "i_o": "B1_13",
          "port": 12
        },
        "dac_14": {
          "type": "B_DAC_Channel",
          "i_o": "B1_14",
          "port": 13
        },
        "dac_15": {
          "type": "B_DAC_Channel",
          "i_o": "B1_15",
          "port": 14
        },
        "dac_16": {
          "type": "B_DAC_Channel",
          "i_o": "B1_16",
          "port": 15
        },
        "dac_17": {
          "type": "B_DAC_Channel",
          "i_o": "B1_17",
          "port": 16
        },
        "dac_18": {
          "type": "B_DAC_Channel",
          "i_o": "B1_18",
          "port": 17
        },
        "dac_19": {
          "type": "B_DAC_Channel",
          "i_o": "B1_19",
          "port": 18
        },
        "dac_20": {
          "type": "B_DAC_Channel",
          "i_o": "B1_20",
          "port": 19
        },
        "dac_21": {
          "type": "B_DAC_Channel",
          "i_o": "B1_21",
          "port": 20
        },
        "dac_22": {
          "type": "B_DAC_Channel",
          "i_o": "B1_22",
          "port": 21
        },
        "dac_23": {
          "type": "B_DAC_Channel",
          "i_o": "B1_23",
          "port": 22
        },
        "dac_24": {
          "type": "B_DAC_Channel",
          "i_o": "B1_24",
          "port": 23
        }
    }
    },
    "trigger_inputs": {
      "type": "B_DAC_TrigChannel",
      "pins": {
        "ext": {
          "type": "B_DAC_TrigChannel",
          "i_o": "",
          "port": "1"
        },
     
      }
    }
  },
}