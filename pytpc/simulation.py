"""
simulation.py
=============

Contains code for simulating the tracking of particles in a TPC.

"""

from __future__ import division, print_function
import numpy
from math import atan2, log, sin, cos, sqrt

import copy

from pytpc.constants import *
import pytpc.relativity as rel


class Gas(object):
    """Describes a gas in the detector.

    Parameters
    ----------
    molar_mass : number
        Provided in g/mol
    num_electrons : int
        Number of electrons per molecule, or the total Z
    mean_exc_pot : float
        The mean excitation potential, as used in Bethe's formula, in eV
    pressure : float
        The gas pressure in Torr
    """

    def __init__(self, molar_mass, num_electrons, mean_exc_pot, pressure):
        self.molar_mass = molar_mass
        self.num_electrons = num_electrons
        self.mean_exc_pot = mean_exc_pot
        self.pressure = pressure

    @property
    def density(self):
        """Density in g/cm^3"""
        return self.pressure / 760. * self.molar_mass / 24040.

    @property
    def electron_density(self):
        """Electron density per cm^3"""
        return N_avo * self.num_electrons * self.density / self.molar_mass

    @property
    def electron_density_per_m3(self):
        """Electron density per m^3"""
        return self.electron_density * 1e6


class Particle(object):
    """ Describes a beam particle for tracking.

    Parameters
        ----------
        mass_num : int
            The A value of the particle (total number of nucleons)
        charge_num : int
            The Z value of the particle
        energy_per_particle : float
            Energy per nucleon, in MeV/u
        position : array-like
            The initial position of the particle
        azimuth : float
            The azimuthal angle of the particle's trajectory, in radians
        polar : float
            The polar angle of the particle's trajectory, in radians
    """

    def __init__(self, mass_num, charge_num, energy_per_particle=0, position=(0, 0, 0), azimuth=0, polar=0):
        self.mass_num = mass_num
        self._mass = mass_num * p_mc2
        self.charge_num = charge_num
        self.charge = charge_num * e_chg
        self.position = numpy.array(position)
        self._energy = energy_per_particle * self.mass_num
        self._mom_mag = sqrt((self._energy + self.mass)**2 - self.mass**2)
        self._momentum = self._mom_mag * numpy.array([cos(azimuth) * sin(polar),
                                                      sin(azimuth) * sin(polar),
                                                      cos(polar)])

    @property
    def momentum(self):
        """The particle's momentum in MeV/c"""
        return self._momentum

    @momentum.setter
    def momentum(self, new):
        self._momentum = new
        self._mom_mag = numpy.linalg.norm(new)
        self._energy = sqrt(self._mom_mag**2 + self.mass**2) - self.mass

    @property
    def momentum_si(self):
        """The particle's momentum in kg.m/s"""
        return self._momentum * 1e6 * e_chg / c_lgt

    @momentum_si.setter
    def momentum_si(self, value):
        self.momentum = value * 1e-6 / e_chg * c_lgt

    @property
    def azimuth(self):
        """The azimuthal angle of the trajectory in radians"""
        px, py, pz = self.momentum
        return atan2(py, px)

    @property
    def polar(self):
        """The polar angle of the trajectory in radians"""
        px, py, pz = self.momentum
        return atan2(sqrt(px**2 + py**2), pz)

    @property
    def energy(self):
        """The total energy in MeV"""
        return self._energy

    @energy.setter
    def energy(self, new):
        self._mom_mag = sqrt((new + self.mass)**2 - self.mass**2)
        self._momentum = self._mom_mag * numpy.array([cos(self.azimuth) * sin(self.polar),
                                                      sin(self.azimuth) * sin(self.polar),
                                                      cos(self.polar)])
        self._energy = sqrt(self._mom_mag**2 + self.mass**2) - self.mass

    @property
    def energy_j(self):
        """The total energy in J"""
        return self.energy * 1e6 * e_chg

    @energy_j.setter
    def energy_j(self, value):
        self.energy = value / 1e6 / e_chg

    @property
    def energy_per_particle(self):
        """The energy per particle in MeV/u"""
        return self.energy / self.mass_num

    @energy_per_particle.setter
    def energy_per_particle(self, new):
        self.energy = new * self.mass_num

    @property
    def mass(self):
        """The particle mass in MeV/c^2"""
        return self._mass

    @property
    def mass_kg(self):
        """The particle mass in kg"""
        return self._mass * MeVtokg

    @property
    def velocity(self):
        """The particle's velocity in m/s"""
        p_si = self.momentum * 1e6 / c_lgt * e_chg
        return p_si / (self.gamma * self.mass_kg)

    @velocity.setter
    def velocity(self, new):
        gam = rel.gamma(new)
        p_si = gam * self.mass_kg * new
        self.momentum = p_si / e_chg * c_lgt * 1e-6

    @property
    def beta(self):
        """The particle's beta, or v/c"""
        en = self.energy
        m = self.mass
        return rel.beta(en, m)

    @property
    def gamma(self):
        """The particle's gamma, as defined in the Lorentz transform"""
        try:
            g = 1 / sqrt(1 - self.beta**2)
        except ZeroDivisionError:
            g = 1e100

        return g

    @property
    def state_vector(self):
        """The state vector of the particle, as (x, y, z, px, py, pz).

        Setting to this will update every other property automatically.
        """
        return numpy.hstack([self.position, self.momentum])

    @state_vector.setter
    def state_vector(self, new):
        self.position = new[0:3]
        self.momentum = new[3:6]


def lorentz(vel, ef, bf, charge):
    """Calculate the Lorentz (electromagnetic) force.

    This is defined as

    ..  math::
        \\mathbf{F} = q(\\mathbf{E} + \\mathbf{v} \\times \\mathbf{B})

    ..  note::
        The units of the inputs must be consistant to get a consistant result.

    Parameters
    ----------
    vel : array-like
        The velocity
    ef : array-like
        The electric field
    bf : array-like
        The magnetic field
    charge : float or int
        The charge of the particle

    Returns
    -------
    force : array-like
        The electromagnetic force, in whatever units the inputs generate
    """

    vx, vy, vz = vel
    ex, ey, ez = ef
    bx, by, bz = bf

    fx = charge * (ex + vy*bz - vz*by)
    fy = charge * (ey + vz*bx - vx*bz)
    fz = charge * (ez + vx*by - vy*bx)

    return numpy.array([fx, fy, fz])


def bethe(particle, gas):
    """ Find the stopping power of the gas.

    Parameters
    ----------
    particle : Particle
        The particle to be tracked. This will **not** be changed by the function.
    gas : Gas

    Returns
    -------
    dedx : float
        The stopping power in MeV/m

    """
    ne = gas.electron_density_per_m3
    z = particle.charge_num
    I = gas.mean_exc_pot * 1e-6  # convert to MeV

    beta_sq = particle.beta**2

    if beta_sq == 0.0:
        # The particle has stopped, so the dedx should be infinite
        dedx = float('inf')
    elif beta_sq == 1.0:
        # This is odd, but then I guess dedx -> 0
        dedx = 0
    else:
        frnt = ne * z**2 * e_chg**4 / (e_mc2 * MeVtokg * c_lgt**2 * beta_sq * 4 * pi * eps_0**2)
        lnt = log(2 * e_mc2 * beta_sq / (I * (1 - beta_sq)))
        dedx = frnt*(lnt - beta_sq)  # this should be in SI units, J/m

    return dedx / e_chg * 1e-6  # converted to MeV/m


def threshold(value, threshmin=0.):
    """Applies a threshold to the given value.

    Any value less than threshmin will be replaced by threshmin.

    Parameters
    ----------
    value : number
        The value to be thresholded
    threshmin : number
        The minimum threshold value

    """
    if value < threshmin:
        return threshmin
    else:
        return value


def find_next_state(particle, gas, ef, bf, tstep):
    """ Find the next step for the given particle and conditions.

    Parameters
    ----------
    particle : Particle
        The particle to be tracked. It will **not** be changed by this function.
    gas : Gas
        The gas in the detector
    ef : array-like
        The electric field
    bf : array-like
        The magnetic field
    tstep : float
        The time step to the next state

    Returns
    -------
    state_vector : ndarray
        The new state vector. The format of this is (x, y, z, px, py, pz). This
        can be provided to an instance of the Particle class to update its state
        in a tracking loop.
    """

    en = particle.energy
    mom = particle.momentum_si
    vel = particle.velocity
    charge = particle.charge

    beta = particle.beta
    if beta == 0:
        return particle.state_vector

    force = lorentz(vel, ef, bf, charge)
    new_mom = mom + force * tstep
    stopping = bethe(particle, gas)  # in MeV/m
    de = float(threshold(stopping*pos_step, threshmin=1e-3))

    if stopping <= 0 or de == 0:
        new_state = particle.state_vector
        new_state[3:6] = [0, 0, 0]  # Set the momentum to 0
        return new_state
    else:
        en = float(threshold(en - de, threshmin=0.))

        new_particle = copy.copy(particle)
        new_particle.momentum_si = new_mom
        new_particle.energy = en

        new_vel = new_particle.velocity
        new_particle.position += new_vel * tstep

        return new_particle.state_vector


def track(particle, gas, ef, bf):
    """ Track the provided particle's trajectory until it stops.

    This can be used to simulate a particle's motion through the detector.

    Parameters
    ----------
    particle : Particle
        The particle to be tracked.
    gas : Gas
        The gas in the detector
    ef : array-like
        The electric field
    bf : array-like
        The magnetic field

    Returns
    -------
    pos : ndarray
        The position of the particle at each step
    mom : ndarray
        The momentum at each step
    time : ndarray
        The time at each step
    en : ndarray
        The energy per particle at each step
    azi : ndarray
        The azimuthal angle of the trajectory at each step
    pol : ndarray
        The polar angle of the trajectory at each step

    """
    pos = []
    mom = []
    time = []
    en = []
    azi = []
    pol = []

    current_time = 0

    pos.append(particle.position)
    mom.append(particle.momentum)
    time.append(current_time)
    en.append(particle.energy_per_particle)
    azi.append(particle.azimuth)
    pol.append(particle.polar)

    while True:
        tstep = pos_step / (particle.beta * c_lgt)
        state = find_next_state(particle, gas, ef, bf, tstep)
        particle.state_vector = state
        if particle.energy == 0:
            print('Particle stopped')
            break

        pos.append(particle.position)
        mom.append(particle.momentum)
        en.append(particle.energy_per_particle)
        azi.append(particle.azimuth)
        pol.append(particle.polar)

        current_time += tstep
        time.append(current_time)

        if particle.position[2] > 1 or sqrt(particle.position[0]**2 + particle.position[1]**2) > 0.275:
            print('Particle left chamber')
            break

    return list(map(numpy.array, (pos, mom, time, en, azi, pol)))