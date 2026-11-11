# DWG Checker Privacy Policy

*Effective November 10, 2026*

DWG Checker is a desktop app that helps instructors grade CAD drawings (DWG files)
against reference solutions and spot possibly copied work. **It does not collect,
send or share any information. Everything it reads and creates stays on your
computer.**

## What the app reads

DWG Checker reads only the files and folders you choose: reference solutions and
student submissions. From those files it uses:

- the file names, which can include students' names when they come from a
  learning management system download;
- the drawing contents (geometry, layers, dimensions and similar);
- information stored inside DWG files: the "Author" and "Last saved by" fields,
  creation and editing times, and file identifiers.

It uses this information only to grade the drawings and to point out files that
may have been shared or copied. It shows the results in the app and in files you
choose to export.

## What the app stores on your computer

- **Settings:** recently used files and folders, the grading settings for each
  reference solution, the window layout, and where AutoCAD is installed. They are
  kept in your user settings (the Windows registry, or the app's preferences file
  on macOS).
- **Orthographic view layouts:** if you edit them, a `.views.json` file is saved
  next to the reference solution.
- **Read-only copies of submissions you open in AutoCAD:** saved in your temporary
  folder (`DwgChecker-evidence`), so the original file can't be changed. They stay
  there until you or your operating system clear temporary files.
- **Temporary copies made while reading files:** deleted automatically.
- **Exports** (results as CSV, settings as JSON): only where you save them.

## What the app does not do

DWG Checker makes no network connections. It has no accounts, advertising,
analytics, telemetry or tracking, and it sends no information to the developer or
anyone else.

## Other software it uses

- **dwgread** (from GNU LibreDWG) comes with the app and runs on your computer to
  read DWG files.
- **AutoCAD**, if you use "Open in AutoCAD": the file is opened in your own copy
  of AutoCAD, and Autodesk's privacy policy applies to AutoCAD.
- **ODA File Converter**, only if you set it up as a fallback for files that can't
  be read. It runs on your computer under the Open Design Alliance's terms.

## Microsoft Store and Windows

If you install DWG Checker from the Microsoft Store, Microsoft handles the
download and installation under the
[Microsoft Privacy Statement](https://privacy.microsoft.com/privacystatement).
Depending on your Windows diagnostic data settings, Windows may send Microsoft a
report if the app crashes, and Microsoft may give the developer technical details
of such crashes, such as where in the program they happened.

## Student records

Because DWG Checker keeps everything on your computer, student work and grading
results stay wherever you keep them. Results you export, and the copies in your
temporary folder, should be handled like other student records under your
institution's policies.

## Changes

If this policy changes, the new version will be posted here with a new effective
date.

## Contact

Joshua Hertz, [j.hertz@northeastern.edu](mailto:j.hertz@northeastern.edu)
