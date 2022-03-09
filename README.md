Realsenseを利用して，特定の色領域のデプスを返すプログラム．

**実行ファイルだけを動かす場合に必要なもの**

Realsense SDK
https://www.intelrealsense.com/sdk-2/

Microsoft Visual C++ 2015 再頒布可能パッケージ Update 3 RCを導入

https://www.microsoft.com/ja-JP/download/details.aspx?id=52685

（参考）https://volx.jp/msvcp140-dll-install

最新が入っていると導入できない場合があるので，そのときは最新版をアンインストール


**使い方**

画像上を左クリックで，そのときのカラー情報を利用してトラッキングを開始．
ROIサイズ内のそのカラーの重心を計算し，その位置でのデプス情報を返す．

右クリックをすると，その時のROI重心の深さを0としてオフセットを引く．

スペースキーを押すと，トラッキング開始時以降の深さ情報(mm)と，その時のトラッキング位置（x,yピクセル）を時刻とともにcsvで吐き出す．
実行ファイルと同じファイルに，output.csvとして吐き出される（ハズ）

ダブルクリックか「i」を押すとc:\\picture.jpgに置かれた画像ファイルをROI領域の中心に一致するように描画する．
用意した画像のサイズと等倍での表示（事前に適当なサイズにリサイズする必要がある）


**開発時には，OpenCVの導入が必要**
↓このあたりを参考に，インクルードパスとライブラリパスの設定，ライブラリのリンク設定が必要
https://www.atmarkit.co.jp/ait/articles/1606/01/news195.html