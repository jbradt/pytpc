from setuptools import setup

setup(
    name='pytpc',
    version='0.9.0',
    description='Tools for analyzing TPC events in Python',
    author='Joshua Bradt',
    author_email='bradt@nscl.msu.edu',
    url='https://github.com/attpc/pytpc',
    packages=['pytpc'],
    install_requires=['scipy',
                      'numpy',
                      'h5py',
                      'tables'],
    package_data={'pytpc': ['data/gases/*', 'data/raw/*']},
    extras_require={
        'docs': ['sphinx_bootstrap_theme>=0.4.5', 'sphinx>=1.2'],
        'plots': ['matplotlib', 'seaborn'],
    },
    )
