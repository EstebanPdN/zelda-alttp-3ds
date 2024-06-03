# zelda3-android
A port of Zelda3 to Android. <br>

Original Repository: https://github.com/snesrev/zelda3 <br>

Use the instructions on the original repository (or below if you don't have access to a computer) to extract the zelda3_assets.dat file from your rom and put it in Android/data/com.dishii.zelda3/files <br>
Running the app once will create the directory. <br>

Android 13 users: check the releases tab for the Android 13 version of the app. 

NOTE: Controller only. No touch controls yet. <br>

How to Change Settings: <br>
Android/data/com.dishii.zelda3/files contains zelda3.ini. Use a text editor to change options. <br>

Default Settings:
L3 Turbo button <br>
18:9 Aspect Ratio <br>
Fullscreen(no android on-screen controls) <br>

<h3>Instructions for creating zelda3_assets.dat on android:</h3>
1. Download PyDroid: https://play.google.com/store/apps/details?id=ru.iiec.pydroid3&hl=en_US. Choose to skip any options that ask for money, you can do all of the following steps without paying.<br>
2. Open the hamburger menu at the top left of the app and select Pip.<br>
3. Type in "Pillow" without the quotes and it will have you install the repository app from the app store.<br>
4. Once the repository app is installed, you can install "Pillow" and "pyyaml" <br>
5. Download the <b>source code<b> zip file for zelda3 at https://github.com/snesrev/zelda3/releases/tag/v0.3. The zip file with the exe file in it will not work. <br>
6. Extract the zip file. <br>
7. Place your rom file in the main zelda3 directory that you extracted, the same one as extract_assets.bat, and rename it to zelda3.sfc <br>
8. Open PyDroid again, open the hamburger menu, and select Terminal.<br>
9. Navigate to where you placed the rom file. (If you are unfamiliar with terminal commands, "ls" lists the folders and files and "cd Foldername" changes the directory. An example using the 0.3 release of zelda3 above would be "cd Download" "cd zelda3-0.3" "cd zelda3-0.3" or simply "cd Download/zelda3-0.3/zelda3-0.3") <br> 
10. Paste in this command <code>python3 assets/restool.py --extract-from-rom</code> <br>
11. It should pause for a while and when it finishes you should be able to see zelda3_assets.dat in the same folder as your rom. You can go ahead and copy that to the Android/data/com.dishii.zelda3/files location. <br>

