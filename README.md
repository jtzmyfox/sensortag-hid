# Sensortag as HID

Emulates right and left mouse click with TI sensortags.

Tested with :
- Sensortag CC2650
- Sensortag CC2541

To use :

Make sure that you bluez is version 5 :

>bluetoothctl --version
5.37

Make sure experimental support is enabled with bluez : 

> cat /lib/systemd/system/bluetooth.service
[...]
ExecStart=/usr/local/libexec/bluetooth/bluetoothd -E
[...]

If not, update your service file and restart the bluetooth service : 
> systemctl daemon-reload
> systemctl restart bluetooth.service

Before using your sensortag, it must have been connected once to your system :

> bluetoothctl
>  scan on

Push on the sensortag's power button to start advertising

> scan off
> connect <your sensortag's macaddr>
> quit

You can now launch sensortag-hid : 

sudo ./sensortag-hidd

# TODO

Currently there might be issues if more than one sensortag is present in your
known-devices database. Be sure to remove the sensortag you don't use using :
> bluetoothctl
> list
> remove <unused sensortag macaddr>
