#!/usr/bin/env python3
"""
Setup script for Digipet
"""

from setuptools import setup, find_packages
from pathlib import Path

# Read README for long description
readme_file = Path(__file__).parent / "README.md"
long_description = readme_file.read_text() if readme_file.exists() else ""

setup(
    name="digipet",
    version="1.0.0",
    description="Autonomous Penetration Testing Platform with Digimon-inspired Evolution",
    long_description=long_description,
    long_description_content_type="text/markdown",
    author="Thomas",
    author_email="",
    url="https://github.com/YOUR_USERNAME/digipet",
    packages=find_packages(),
    include_package_data=True,
    install_requires=[
        "Flask>=3.0.0",
        "flask-cors>=4.0.0",
        "bcrypt>=4.1.2",
        "Pillow>=10.1.0",
        "psutil>=5.9.6",
    ],
    extras_require={
        'dev': [
            'pytest>=7.0.0',
            'black>=23.0.0',
            'flake8>=6.0.0',
        ],
        'rpi': [
            # Uncomment when deploying to actual Raspberry Pi
            # 'RPi.GPIO>=0.7.1',
            # 'spidev>=3.6',
        ]
    },
    python_requires=">=3.7",
    entry_points={
        'console_scripts': [
            'digipet=digipet_daemon:main',
        ],
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Information Technology",
        "Topic :: Security",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
    ],
    keywords="pentesting security automation hacking network",
    project_urls={
        "Bug Reports": "https://github.com/YOUR_USERNAME/digipet/issues",
        "Source": "https://github.com/YOUR_USERNAME/digipet",
        "Documentation": "https://github.com/YOUR_USERNAME/digipet#readme",
    },
)
