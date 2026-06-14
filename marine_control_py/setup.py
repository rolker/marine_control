from setuptools import find_packages, setup

package_name = 'marine_control_py'

setup(
    name=package_name,
    version='0.0.1',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Roland Arsenault',
    maintainer_email='roland+claude-code@ccom.unh.edu',
    description=('Python device-control library (rclpy sibling of '
                 'marine_control::ControlServer) for marine_control adopters.'),
    license='Apache-2.0',
    tests_require=['pytest'],
)
