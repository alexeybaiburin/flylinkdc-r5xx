del .\compiled\FlylinkDC.exe
del .\compiled\FlylinkDC.pdb
call UpdateRevision.bat %1 %2 %3 %4
if errorlevel 1 goto :error

call tools\ExtractVersion.bat %1 %2 %3 %4
if errorlevel 1 goto :error

rem VS150COMNTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build
rem C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\IDE\
rem C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\Tools - vsdevcmd.bat
rem %comspec% /k "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\Tools\VsDevCmd.bat"

rem call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\Tools\vsdevcmd.bat"

"C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\IDE\devenv.exe" FlylinkDC_2017.sln /Rebuild "Release|Win32"
rem chcp 437
rem msbuild FlylinkDC_2017.sln /t:Rebuild /p:COnfiguration="Release" /p:Platform="Win32" /v:diag

if not exist .\compiled\FlylinkDC.exe goto :builderror

rem "C:\Program Files (x86)\Microsoft SDKs\Windows\v7.0A\Bin\signtool.exe" sign /a /d "FlylinkDC++ r5xx" /du "http://flylinkdc.blogspot.com" /t http://timestamp.verisign.com/scripts/timstamp.dll ".\compiled\FlylinkDC.exe"
"C:\Program Files (x86)\Windows Kits\8.1\bin\x86\signtool.exe" sign /v /d "FlylinkDC++ r5xx" /du "http://flylinkdc.blogspot.com" /fd sha1 /t http://timestamp.verisign.com/scripts/timstamp.dll ".\compiled\FlylinkDC.exe"
"C:\Program Files (x86)\Windows Kits\8.1\bin\x86\signtool.exe" sign /as /v /d "FlylinkDC++ r5xx" /du "http://flylinkdc.blogspot.com" /fd sha256 /tr http://timestamp.comodoca.com/rfc3161 /td sha256 ".\compiled\FlylinkDC.exe"

rem call Tools\MakePortalBrowserFileList.bat
call src_gen_filename.bat -x86
7z.exe a -r -t7z -m0=lzma -mx=9 -mfb=512 -md=1024m -ms=on -x@src_exclude_hard.txt -ir@src_include_bin_x86.txt %FILE_NAME%.7z compiled/FlylinkDC.exe 
rem -i@PortalBrowserFileList.txt 

svn log > changelog-flylinkdc-r5xx-svn.txt

for /l %%i in (1,1,20) do echo @tools\replace_str.vbs " | %%i lines" "" changelog-flylinkdc-r5xx-svn.txt

call src_gen_filename.bat -src
7z.exe a -r -t7z -m0=lzma -mx=9 -mfb=512 -md=1024m -ms=on %FILE_NAME%.7z -i@src_include.txt -x@src_exclude.txt -x!./setup/*

goto :end

:error
echo Can't update/extract version/revision
pause
goto :end

:builderror
echo Compilation error. Building terminated.
pause

:end
