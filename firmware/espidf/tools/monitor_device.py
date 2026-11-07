#!/usr/bin/env python3
"""USB console with native USB reset signals deasserted."""
import argparse
import sys

parser = argparse.ArgumentParser()
parser.add_argument("port")
args = parser.parse_args()
try:
    import serial.tools.miniterm
except ImportError:
    sys.exit("Instale: py -m pip install pyserial")
sys.argv = ["miniterm", "--dtr", "0", "--rts", "0", "--eol", "CRLF",
            args.port, "115200"]
print("Digite setup e pressione Enter para mostrar as credenciais locais.")
print("Digite status para diagnóstico. Para sair do monitor: Ctrl + ].")
serial.tools.miniterm.main()
