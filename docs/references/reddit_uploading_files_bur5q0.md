# Uploading files to the RCD 330 Noname/Visteon

Source: https://www.reddit.com/r/RCD_330/comments/bur5q0/uploading_files_to_the_rcd_330_nonamevisteon/

## Original Post

[:first-child]:h-full [&>:first-child]:w-full [&>:first-child]:mb-0 [&>:first-child]:rounded-[inherit] h-full w-full [&>:first-child]:overflow-hidden [&>:first-child]:max-h-full"
>

Go to RCD_330](/r/RCD_330/)

[r/RCD_330

:first-child]:h-full [&>:first-child]:w-full [&>:first-child]:mb-0 [&>:first-child]:rounded-[inherit] h-full w-full [&>:first-child]:overflow-hidden [&>:first-child]:max-h-full"
>

r/RCD_330

Volkswagen VW Radio RCD 330 / RCD 340 Enthousiastic subreddit.
B - E versions, A version, Windows CE, Linux.
Visteon, Desay, Noname, Tianboa...
With Carplay & Android Auto

Members

Online

•

571Pro

Uploading files to the RCD 330 Noname/Visteon

Firmware](/r/RCD_330/)

It's possible to replace files.

For this you need to use the file as a .tar extension including;

\opt\stdplus.ini

\script\after_app_update.sh

\script\before_app_update.sh

\usr\

\config.txt

after_app_update.sh contains

#!/bin/sh

echo after_app_update

before_app_update.sh contains

#!/bin/sh

#created by yournickname

echo before_app_update

echo "Some text you want"

cp /sysupdate/system/script/fileyouwanttoadd /opt/gresfiles/

echo "Done..."

cp /syssupdate is the source path and after for example /opt/gresfiles/ is the destination

If we look into the 5518 V2 which everyone is complaining about that it have a bootlogo while they have paid for it.

Then we see there is added a line

dd if=/sysupdate/system/script/system of=/dev/mtdblock5

And a file called "system" what is the bootlogo and used from the script of [priZrakinside

\script\after_app_update.sh

\script\before_app_update.sh

\script\system](https://4pda.ru/forum/index.php?showuser=506261)

Also the used computer had a set date so that all files were like adjusted the same time. And none were newer then 2017.

What can be seen as clever or more as fooling the buyers.

[](https://preview.redd.it/x4bwndd0kb131.png?width=798&format=png&auto=webp&s=6c5a09b22de31f0a49a38e51499fffc1d43f3109)

Edit;

found with a hexeditor that the HMI file is tagged. (this is also the file that was used for the translations hex by hex)

\opt\hmi

Starting from address 00427940 till 00427970

[](https://preview.redd.it/5epm9khsip131.png?width=1525&format=png&auto=webp&s=c1a404fed0bb3e3299b30bad087f2a3d66c96a05)

Please if you have other findings please report, or share!

Share


## Comments

### u/hattothemoon

Hi I was actually looking at the older (original) firmware yesterday and it seems like the kernel has been changed. How is this possible as kernel source is not available and latest VM firmware is 5314. Are the newer versions sourced from original version of firmware? I tried looking at the Russian forum, but unfortunately Google translate doesn't work well and I only read English. I was also taking a look at the Android auto config file and it seems like 720p and 1080p were supported on old unmodified firmware but was removed. Also there was some undocumented change of number of pre-rendered frames. I haven't had a chance yet to look through everything yet.


### u/571Pro

I haven't looked that far to be honest, but this seems indeed interesting.

At the moment I don't have a unit so in this case they can keep saying it is the wrong firmware. But they sold it to us as "for sure V2". If this is not the case then they are even fooling their customers.

Also this is weird in the before_app_update.sh, so in 5515 it removes the hmi file and copies a new one and in 5518 it leaves it there and probably overwrites the blocks.

With 5515

#!/bin/sh

echo before_app_update

rm -rf /opt/hmi

rm -rf /opt/gresfiles/*

and 5518

#!/bin/sh

echo before_app_update

rm -rf /opt/gresfiles/*

