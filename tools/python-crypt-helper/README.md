somebody requested a quick python tool to decipher text so here it is

# requirements
python3

# setup
1. open terminal in this directory
    - on Windows be sure to use Powershell
1. run the following:
    - if on windows: `py -m venv .venv`
    - macos or linux: `python3 -m venv .venv`
1. then the following
    - windows: `.\.venv\bin\Activate.ps1`
    - macos or linux(assuming sh type shell): `. .venv/bin/activate`
1. now `pip install -r requirements.txt`
1. now you can run the program `python3 decipher.py`
