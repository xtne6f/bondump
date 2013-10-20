bondump - 任意のBonDriverを標準出力にダンプする

Usage: bondump -d driver [-c ch][-s space][-m ch_map][-b buf_size>=64][-p]

[オプション]
-d  {BonDriver_*.dllの絶対パスかbondumpからの相対パス}
-c  {チャンネル番号}
    物理チャンネルと一致しているとは限らない(Driverの実装依存)
-s  {チューニング空間の番号}
    -c 0 -s 99 で一覧がでるはず
-m  {-cで指定するチャンネル}c{Driverに渡すチャンネル}s{Driverに渡すチューニング空間}
    チャンネルとチューニング空間のレイアウトをカスタマイズできる
    複数指定可(前方のものを優先)。-sを指定しているときは無視される
    たとえば -m 50c0s1 -m 13c0s0 ならDriverに実際に渡すチャンネルとチューニング空間をつぎのように変換する
    ・-cで50以上を指定した => チャンネル={-cで指定したチャンネル}-50, チューニング空間=1
    ・-cで13以上を指定した => チャンネル={-cで指定したチャンネル}-13, チューニング空間=0
-b  {内部バッファのサイズ(キロバイト)}
    64以上。デフォルトは4096
-p  ソース参照

[その他]
・デバッグに使っていた断片コードを集めて整えたもの
・標準出力にダンプするのでリダイレクトしないと端末がえらいことになる
・終わらせ方: 端末で Ctrl+C/CTRL+BREAK、もしくは taskkill /im bondump.exe
・チャンネル変更: 端末(標準入力)で c {チャンネル} {チューニング空間(-m指定時は非必須)}{改行}
・失敗するときは echo %ERRORLEVEL% の結果をソースの // Command result と照合してください
・ビルド環境はVC++2010SP1を想定。以下を順番に入れる:
  1.Visual C++ 2010 Express (Webインストール)
    http://go.microsoft.com/fwlink/?LinkId=190491
  2.Visual Studio 2010 Service Pack 1 (Webインストール)
    http://www.microsoft.com/ja-jp/download/details.aspx?id=23691
  (x64ビルドでは以下も)
  3.Windows SDK for Windows 7 (Webインストール)
    http://www.microsoft.com/en-us/download/details.aspx?id=8279
    # Visual C++ Compilers のチェックを外してインストールする。でないと失敗する
    # Visual C++ x86/x64 2010 Redistributable を事前にアンインストールしておく必要があるかもしれない
  4.Windows SDK 7.1 用 Microsoft Visual C++ 2010 Service Pack 1 コンパイラ更新プログラム
    http://www.microsoft.com/ja-jp/download/details.aspx?id=4422
    # C++ Compilers を改めてインストールするため
