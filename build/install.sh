# Change to script directory so that the script can be called from anywhere.
cd "$(dirname "$0")"

cd ..

# Colors to make this script look nice.
BLUE='\e[94m';
NO_COLOR='\033[0m';

echo ""
echo "${BLUE}" \
"¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯\n" \
"               UCLA Radio Lights installation script               \n" \
"___________________________________________________________________\n" \
"${NO_COLOR}" \


# Download all submodules.
git submodule init;
git submodule update --recursive;

# Install required packages.
REQUIRED_PACKAGES="build-essential python python-pip"
echo "${BLUE}Installing necessary system packages:${NO_COLOR}
  ${REQUIRED_PACKAGES}"

sudo apt-get install ${REQUIRED_PACKAGES}

PIP_REQUIREMENTS_FILE="build/pip_requirements.txt"
echo "\n${BLUE}Installing pip requirements from file:${NO_COLOR}
  ${PIP_REQUIREMENTS_FILE}"
sudo -H pip install -I -r ${PIP_REQUIREMENTS_FILE}

echo "\n${BLUE}Installation complete.${NO_COLOR}";
