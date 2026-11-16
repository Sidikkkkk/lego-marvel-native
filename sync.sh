#!/bin/bash
MSG=${1:-"Update universal native loader"}
git add .
git commit -m "$MSG"
git push origin main
echo ">>> Berhasil push ke GitHub! Cek tab Actions untuk download APK baru."
