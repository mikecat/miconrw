# GD32VF103CBT6の読み書き

GD32VF103CBT6は IchigoJam R に使われている他、[秋月電子通商](https://akizukidenshi.com/catalog/top.aspx)で売られている開発ボード(K-14678)にも搭載されている。

GD32VF103CBT6の資料(Data Sheet：ピンの仕様など と Manual：各モジュールの使い方など)は、以下から入手できる。

[GD32VF103CBT6](https://www.gigadevice.com/microcontroller/gd32vf103cbt6/)

しかし、これらの資料ではデータの読み書きのしかたはわからなかった。

さて、GD32VF103CBT6は `stm32flash` というアプリケーションを用いて書き込みができるらしい。

* [IchigoJam BASIC 1.5β1、USBキーボード対応 RISC-V版 IchigoJam Rβ 出荷スタート！ #KidsIT #IchigoJam / 福野泰介の一日一創 / Create every day by Taisuke Fukuno](https://fukuno.jig.jp/3103)
* [RISC-V 搭載 IchigoJam 関連 - イチゴジャム レシピ](https://15jamrecipe.jimdofree.com/%E5%91%A8%E8%BE%BA%E6%A9%9F%E5%99%A8/%E3%83%91%E3%82%BD%E3%82%B3%E3%83%B3%E3%81%A8%E6%8E%A5%E7%B6%9A/risc-v/)

そこで、RM0090 (STM32F405/415などの資料) を見てみた。
すると、2.4 Boot configuration の Embedded bootloader に、AN2606を見ろという記述があった。

AN2606 は、ブートローダの呼び出し方が書かれていたが、具体的な通信についての説明は見当たらなかった。
AN2606 の 2 Related documents で紹介されていたAN3155に、シリアル通信を用いた読み書きの方法が書かれていた。

これらのRM0090・AN2606・AN3155はSTM32F405/415に関する資料であるが、
AN3155で紹介されている方法を試してみると、GD32VF103CBT6でも使えそうであった。

# PCからのシリアル通信

## Windows

* [RS232C　シリアル通信](http://www.ys-labo.com/BCB/2007/070512%20RS232C%20zenpan.html)
* [SetCommState function (winbase.h) - Win32 apps | Microsoft Docs](https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setcommstate)
* [DCB (winbase.h) - Win32 apps | Microsoft Docs](https://docs.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-dcb)

## Linux

未調査

## OS X (Mac)

未調査

## その他

未調査
