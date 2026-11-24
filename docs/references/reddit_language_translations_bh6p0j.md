# WIP - Language / translations RCD 330 C-E types

Source: https://www.reddit.com/r/RCD_330/comments/bh6p0j/wip_language_translations_rcd_330_ce_types/

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

WIP - Language / translations RCD 330 C-E types

Languages](/r/RCD_330/)

Translations can be made in RCD 330 B-E types.

The file with the languages is in the firmware file. You can open this using a file archiver (winrar/7zip...)

[](https://preview.redd.it/ya9kqulxqeu21.png?width=1600&format=png&auto=webp&s=1fd836b74e44c183a5ad5185e501df3b88d99f7c)
firmware file HMI

Using then a hexeditor (for example HxD) you can find the languages aprox in the middle of the file.

[https://mh-nexus.de/en/hxd/ (Freeware)](https://mh-nexus.de/en/hxd/)
hmi with german translation

Be carefull!

- Use a hexeditor not a notepad or variant. At least for this:

- Words cannot exceed their current length. You cannot overwrite other characters what are in the words like punctions or after/before words.

When you exceed or changes too much your firmware will not work, it will flash by downloading to the unit but then gets into a bootloop. Only TTL access will bring the unit back to life.

There is also a difference is firmware versions. But it normally start behind the

==== HMI ==== INFO: main RVC

And ends at

==== HMI ==== INFO: ts_open error

Edit: Engineering and translation of Polish language by [szyzu](https://www.reddit.com/user/szyzu/)
by szyzu

part of the source;

https://reverseengineering.stackexchange.com/questions/17342/adding-additional-language-to-linux-based-firmware-of-volkswagen-car-radio

Share


## Comments

### u/Current1602

Dears,

Please can you help and suggest how I can translate Desay 187 B version?

I upload files from device, and just found out that it no single HMI file for application.

As I understood the HMI working through SVP application and has separate compiled file for each screen.

Also I found out in Storage folder source of .kzb progect files that can be open just by Kanzi Studio and it kind of mixed data files with pictures, code and data.

Just cannot understand if system every start compile application from .kzb files or need to translate some other one.

Please suggest What I can do?

By link you can find uploaded files: https://cloud.mail.ru/public/UeNQ/4Bb1faBrR

Best regards,


### u/571Pro

Thank you for your insights! I think we better make a separate thread for these units...


### u/szyzu

I have analyzed the hmi file and I have some conclusions:

-

This is a Linux ELF executable file and all the strings are in the rodata section.

-

5 languages (English, Chinese[?], Portuguese, Russian[?], Spanish) are hardcoded. The language phrases are stored in this order.

-

The English phrase begins with a declaration of the number of characters. If the length is longer than 30 then the declaration byte is 0x9E and means that after reading 30 bytes there will be another declaration of length.

-

The Chinese phrase starts with a coding declaration (value between 0xE0 and 0xFF or 0x60 and 0x80). A value between 0xE0 and 0xFF means that the reading will be continued, and 0x60 and 0x80 means that the read bytes end the phrase. The number of bytes to be read depends on the difference between the value and the minimum value of the interval. For example, 0xE2 means that 2 bytes must be read.

-

5 Portuguese and Spanish are coded in the same way as English.

-

Russian is coded similarly to Chinese using other control(encoding) bytes.

My proposal is to change the phrases in Portuguese. I think that if Russian language is not used then we can use this place to extend phrases in the target language. Using this knowledge I wrote an application that separated all phrases in English from the file along with their translation into Portuguese and the maximum possible length. The application found 666 phrases (some are incorrectly detected), which are possible to translate by this method. I couldn't find many phrases in the radio interface.

My next step is to prepare an application that will be able to prepare a file with the changed language. Soon I will share with you some more insights


### u/571Pro

Thank you for this clarification so far already! Looking forward to the next work, so we all can participate in translations.

Maybe some linux dev can also found some tweaks we can add to this firmwares...


### u/szyzu

I did some tests and I think that the best idea would be to replace the Chinese language. I am in the process of writing an application that will properly change the hmi file. I think that next week I will be able to finish it and test it while doing my translation.

https://drive.google.com/file/d/1GgPNV_cLO8JeHH01N2yUO9UUhWo7Ol8y/view?usp=drivesdk


### u/571Pro

I asked permission by gmail ;) thanks already so far!

Edit; adjusted beginning post and added the picture from the google drive. Thanks to you!

Hopefully soon a release!


### u/Current1602

Dear Szyzu,

I tried to find the way to translate for Russian language Desday 187B version.

To learn how it could be translated I tried to read Russian translation in Noname 187B version and open hmi file by Hxd (or wxMEdit), but I tried to implemnt different codepage in the system and also set different kind of UTF I still could not get Russian words readable. I even tried to do every thing under Ubuntu system it was also without success.

Please could you give idea how I can see Russian translation in hmi file readable?

Also could you suggest if it could not be read how I can convert from readble Russian words to Hex code to put in Hxd editor?

Thank you in advance,

best regards,


### u/Current1602

Dear Szyzu,

As I Found out the Russian language stored in Hex in some combination of Unicode and UTF-8: It is begining from 0xCx (here x after C is number of bytes) and after each letter coding by byte instead 2 bytes of UTF8 and this byte is second byte from Unicode: Example If Russian Letter "Р" has the code 0x0420 in HMI we have just 0x20.

This some thing strange for me why the used such non standart coding.

Best regards,


### u/szyzu

I'm sorry for the late answer.

You're right. The cyrlic is coded with control bytes Cx and as far as I can remember, it's 8x. x is the number of bytes to read but not more than 30. C means change of coding for the next bytes and the next control code belongs to the same string. 8 means that change of coding and this string ends after reading x bytes.

Why is that? It takes up less memmory and I suspect the compiler did it that way.


### u/szyzu

If you want, I can send you all the texts in Russian extracted from hmi.

I'm very busy right now. If I find some free time I can do it for you. E.g. English and Russian in csv or xls file.


### u/Current1602

n

Dear Szyzu,

Thanks a lot. I just investigate how it was translated in Noname version to find the way to translate Desay 187B.

really I don't need translatad words from the Noname, More I nedd to Know or find the way What tool I need to use for easy coding from Russian words to this kind of Hex coding?

Can you suggest the way how you doing it?

best regards,


### u/szyzu

Unfortunetly I don't have a unit like this to make some tests. I looked quickly at the software and the application is splited into smaller ones. Each application has embeded languages. At the moment I have no conclusions about coding. When I finish my current projects I will look into this software and try to help with translation.


### u/Nice-Raspberry-2134

Hello u/szyzu I have just bought an rcd330 and would like to add the FR language. I saw that the HMI file contains the languages. How can I change the sentences? Thanks in advance


### u/[deleted]

Hey!

I'm interested in the French version.
Do you have any website, or Github repo where we can follow your progress on this?

I would be also happy to assist you with the whole translation process! :)


### u/[deleted]

Done! Thanks a lot!


### u/algerino77

Hi :)

With a friend, 2 years ago we finished the translation into FR on firmware 5522. It works perfectly, but I wanted to update to the new firmware, and take the opportunity to change the replaced language. (Currently we have replaced the Russian language with French, but this is not the right option because they do not use the same alphabet)

Maybe you need help?

What language are you replacing?

I had just downloaded the 5614v3 to get started, but it's useless to be two to do it ;)

Here is the link of our last update which dates from January 2022
https://mega.nz/file/OI4HiIIC#tkcw2gwlWywk7IQJB7PNE3cXYOmc0Bl88I3Khq6D2Cc
You may find help for your translation on the new Firmware ;)


### u/TurnoverNearby9131

Hi szyzu, did you succeed in making an application? I looked around, but I can only see the wip, but not the final result. Do you need help if not finished?


### u/Jazzlike-Will-6492

Me pueden decir que dice

