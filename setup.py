from setuptools import setup, find_packages

setup(name='pytpc',
      version='0.1',
      description='Tools for analyzing TPC events in Python',
      author='Joshua Bradt',
      author_email='bradt@nscl.msu.edu',
      packages=['pytpc'],
      install_requires=['numpy', 'matplotlib', 'filterpy', 'scipy', 'scikit-learn', 'seaborn', 'sphinx',
                        'sphinx-bootstrap-theme'],
      )