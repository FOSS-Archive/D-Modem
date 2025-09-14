# D-Modem
Connect to dialup modems over VoIP using SIP, no modem hardware required.

https://www.aon.com/cyber-solutions/aon_cyber_labs/introducing-d-modem-a-software-sip-modem/

## Building
You'll need Linux along with PJSIP's dependencies (OpenSSL).  The SmartLink DSP library only ships as 32-bit object code, so building requires 32-bit support (e.g., gcc-multilib).  Run `make` from the top-level directory.

## How it Works
Traditional “controller-based” modems generally used a microcontroller and a DSP to handle all aspects of modem communication on the device itself.  Later, so-called “Winmodems” were introduced that allowed for field-programmable DSPs and moved the controller and other functionality into software running on the host PC.  This was followed by “pure software” modems that moved DSP functionality to the host as well.  The physical hardware of these softmodems was only used to connect to the phone network, and all processing was done in software. 

D-Modem replaces a softmodem’s physical hardware with a SIP stack.  Instead of passing audio to and from the software DSP over an analog phone line, audio travels via the RTP (or SRTP) media streams of a SIP VoIP call.   

## Usage
The repository contains two applications: 

slmodemd – A stripped down and patched version of Debian’s sl-modem-daemon package.  All kernel driver code has been replaced with socket-based communication, allowing external applications to manage audio streams. 

d-modem – External application that interfaces with slmodemd to manage SIP calls and their associated audio streams. 

After they have been built, you will need to configure SIP account information in the SIP_LOGIN environment variable: 

    # export SIP_LOGIN=username:password@sip.example.com
Next, run slmodemd, passing the path to d-modem in the -e option.  Use -d<level> for debug logging. 

    # ./slmodemd/slmodemd -d9 -e ./d-modem
    SmartLink Soft Modem: version 2.9.11 Oct 28 2021 16:51:30 
    symbolic link `/dev/ttySL0' -> `/dev/pts/3' created. 
    modem `slamr0' created. TTY is `/dev/pts/3' 
    Use `/dev/ttySL0' as modem device, Ctrl+C for termination.

In another terminal, connect to the newly created serial device at 115200 bps: 

    # screen /dev/ttySL0 115200

You can now interact with this terminal (almost) as you would with a normal modem using standard AT commands.  A similar modem’s manual provides a more complete list. 

Because there isn’t any dial tone on our SIP connection, you’ll need to disable dial tone detection: 

    atx3 
    OK

To successfully connect, you will likely need to manually select a modulation and data rate.  In our testing, V.32bis (14.4kbps) and below appears to be the most reliable, though V.34 (33.6kbps) connections are sometimes successful.  For example, the following command selects V.32bis with a data rate of 4800 – 9600 bps.  Refer to the manual for further details. 

    at+ms=132,0,4800,9600 
    OK

Finally, dial the number of the target system.  Below shows a connection to the NIST atomic clock: 

    atd303-494-4774 
    CONNECT 9600 
    National Institute of Standards and Technology 
    Telephone Time Service, Generator 1b 
    Enter the question mark character for HELP 
                            D  L 
     MJD  YR MO DA HH MM SS ST S UT1 msADV         <OTM> 
    59515 21-10-28 21:40:18 11 0 -.1 045.0 UTC(NIST) * 
    59515 21-10-28 21:40:19 11 0 -.1 045.0 UTC(NIST) * 
    59515 21-10-28 21:40:20 11 0 -.1 045.0 UTC(NIST) * 
    59515 21-10-28 21:40:21 11 0 -.1 045.0 UTC(NIST) * 
    59515 21-10-28 21:40:22 11 0 -.1 045.0 UTC(NIST) *
    59515 21-10-28 21:40:23 11 0 -.1 045.0 UTC(NIST) *

### Calling between two D-Modem clients

The `d-modem` helper launched by `slmodemd` registers your SIP account and waits for incoming calls.
When a call arrives, the terminal prints `RING` and you can answer with `ATA` or hang up with `ATH`.

To connect two clients together:

1. On each machine set `SIP_LOGIN` to credentials for SIP accounts that can call each other.
2. Start `slmodemd` pointing at `d-modem` and open the created `/dev/ttySL0` with a terminal program.
3. Disable dial tone detection (`ATX3`) and set the desired modulation (`AT+MS=...`) on both ends.
4. From the calling side issue `ATD` with the callee's SIP URI or number.
5. The receiving side will show `RING`; answer with `ATA`.
6. Either side can terminate the call with `ATH`.

## Known Issues / Future Work
- Connections are unreliable, and it is currently difficult to connect at speeds higher than 14.4kbps or so.  It might be possible to improve this by disabling/reconfiguring PJSIP’s jitter buffer.
- Additional logging/error handling is needed
- The serial interface could be replaced with stdio or a socket, and common AT configuration options could be exposed as command line options


Copyright 2021 Aon plc
