cimport mcopt
from libcpp.vector cimport vector as cppvec
from libcpp.map cimport map as cppmap
from libcpp.pair cimport pair as cpppair
cimport armadillo as arma
import numpy as np
cimport numpy as np
from cython.operator cimport dereference as deref, preincrement as preinc
from libc.stdio cimport printf


cdef cppvec[double] np2cppvec(np.ndarray[np.double_t, ndim=1] v):
    cdef cppvec[double] res
    cdef double[:] vView = v
    for i in range(v.shape[0]):
        res.push_back(v[i])
    return res


cdef class Tracker:
    """Tracker(mass_num, charge_num, eloss, efield, bfield)

    A class for simulating the track of a charged particle in the AT-TPC.

    Parameters
    ----------
    massNum, chargeNum : int
        The mass and charge numbers of the projectile.
    eloss : ndarray
        The energy loss for the particle, in MeV/m, as a function of projectile energy.
        This should be indexed in 1-keV steps.
    efield, bfield : array-like
        The electric and magnetic fields, in SI units.

    Raises
    ------
    ValueError
        If the dimensions of an input array were invalid.
    """

    cdef mcopt.Tracker *thisptr

    def __cinit__(self, int massNum, int chargeNum, np.ndarray[np.double_t, ndim=1] eloss,
                  np.ndarray[np.double_t, ndim=1] efield, np.ndarray[np.double_t, ndim=1] bfield):
        cdef cppvec[double] elossVec = np2cppvec(eloss)
        cdef arma.vec *efieldVec
        cdef arma.vec *bfieldVec
        try:
            efieldVec = arma.np2vec(efield)
            bfieldVec = arma.np2vec(bfield)
            self.thisptr = new mcopt.Tracker(massNum, chargeNum, elossVec, deref(efieldVec), deref(bfieldVec))
        finally:
            del efieldVec, bfieldVec

    def __dealloc__(self):
        del self.thisptr

    property mass_num:
        """The mass number of the tracked particle."""
        def __get__(self):
            return self.thisptr.getMassNum()

    property charge_num:
        """The charge number of the tracked particle."""
        def __get__(self):
            return self.thisptr.getChargeNum()

    property efield:
        """The electric field in the detector, in V/m."""
        def __get__(self):
            cdef arma.vec efieldVec = self.thisptr.getEfield()
            return arma.vec2np(efieldVec)

    property bfield:
        """The magnetic field in the detector, in Tesla."""
        def __get__(self):
            cdef arma.vec bfieldVec = self.thisptr.getBfield()
            return arma.vec2np(bfieldVec)

    def track_particle(self, double x0, double y0, double z0, double enu0, double azi0, double pol0):
        """Tracker.track_particle(x0, y0, z0, enu0, azi0, pol0)

        Simulate the trajectory of a particle.

        Parameters
        ----------
        x0, y0, z0, enu0, azi0, pol0 : float
            The initial position (m), energy per nucleon (MeV/u), and azimuthal and polar angles (rad).

        Returns
        -------
        ndarray
            The simulated track. The columns are x, y, z, time, energy/nucleon, azimuthal angle, polar angle.
            The positions are in meters, the time is in seconds, and the energy is in MeV/u.

        Raises
        ------
        RuntimeError
            If tracking fails for some reason.
        """
        cdef mcopt.Track tr = self.thisptr.trackParticle(x0, y0, z0, enu0, azi0, pol0)
        cdef arma.mat trmat = tr.getMatrix()
        return arma.mat2np(trmat)


cdef class PadPlane:
    """A lookup table for finding the number of the pad under a certain (x, y) position.

    Parameters
    ----------
    lookup_table : ndarray
        An array of pad number as a function of x (columns) and y (rows).
    x_lower_bound : float
        The x value of the first column of the lookup table.
    x_delta : float
        The x step between adjacent columns.
    y_lower_bound : float
        The y value of the first row of the lookup table.
    y_delta : float
        The y step between adjacent rows.
    rot_angle : float, optional
        An angle, in radians, through which to rotate the pad plane.
    """

    cdef mcopt.PadPlane *thisptr

    def __cinit__(self, np.ndarray[np.uint16_t, ndim=2] lut, double xLB, double xDelta,
                  double yLB, double yDelta, double rotAngle=0):
        cdef arma.Mat[mcopt.pad_t] *lutMat
        try:
            lutMat = arma.np2uint16mat(lut)
            self.thisptr = new mcopt.PadPlane(deref(lutMat), xLB, xDelta, yLB, yDelta, rotAngle)
        finally:
            del lutMat

    def __dealloc__(self):
        del self.thisptr

    def get_pad_number_from_coordinates(self, double x, double y):
        """Look up the pad number under the given point.

        Parameters
        ----------
        x, y : float
            The x and y position to look up.

        Returns
        -------
        int
            The pad number under the given point. This will be whatever value is in the lookup table at that position.
            If the lookup table contains invalid values (e.g. to represent areas that do not contain a pad), then
            the result should be compared to the invalid value to check that it is a valid pad number.

        Raises
        ------
        RuntimeError
            If the point was outside the dimension of the lookup table, or if anything else failed.
        """
        return self.thisptr.getPadNumberFromCoordinates(x, y)


cdef class EventGenerator:
    """A GET event generator. This can be used to generate events from simulated tracks.

    Parameters
    ----------
    pad_plane : PadPlane instance
        The pad lookup table to use when projecting to the Micromegas. The units should be meters.
    vd : array-like
        The drift velocity vector, in cm/us.
    clock : float
        The write clock, in MHz.
    shape : float
        The shaping time, in seconds.
    mass_num : int
        The particle's mass number.
    ioniz : float
        The mean ionization potential of the gas, in eV.
    gain : int
        The gain to apply to the event.
    """

    cdef mcopt.EventGenerator *thisptr

    def __cinit__(self, PadPlane pads, np.ndarray[np.double_t, ndim=1] vd, double clock, double shape,
                  unsigned massNum, double ioniz, double gain, double tilt,
                  np.ndarray[np.double_t, ndim=1] beamCtr = np.zeros(3, dtype=np.double)):
        cdef arma.vec *vdVec
        cdef arma.vec *beamCtrVec

        try:
            vdVec = arma.np2vec(vd)
            beamCtrVec = arma.np2vec(beamCtr)
            self.thisptr = new mcopt.EventGenerator(deref(pads.thisptr), deref(vdVec), clock, shape, massNum, ioniz, gain,
                                              tilt, deref(beamCtrVec))
        finally:
            del vdVec, beamCtrVec

    def __dealloc__(self):
        del self.thisptr

    property mass_num:
        """The mass number of the tracked particle."""
        def __get__(self):
            return self.thisptr.massNum

        def __set__(self, newval):
            self.thisptr.massNum = newval

    property ioniz:
        """The ionization potential of the gas, in eV."""
        def __get__(self):
            return self.thisptr.ioniz

        def __set__(self, newval):
            self.thisptr.ioniz = newval

    property gain:
        """The micromegas gain."""
        def __get__(self):
            return self.thisptr.gain

        def __set__(self, newval):
            self.thisptr.gain = newval

    property tilt:
        """The detector tilt angle, in radians."""
        def __get__(self):
            return self.thisptr.tilt

        def __set__(self, newval):
            self.thisptr.tilt = newval

    property clock:
        """The CoBo write clock frequency, in MHz."""
        def __get__(self):
            return self.thisptr.getClock()

        def __set__(self, double newval):
             self.thisptr.setClock(newval)

    property shape:
        """The shaping time in the electronics, in seconds."""
        def __get__(self):
            return self.thisptr.getShape()

        def __set__(self, double newval):
            self.thisptr.setShape(newval)

    property vd:
        """The 3D drift velocity vector, in cm/µs."""
        def __get__(self):
            return arma.vec2np(self.thisptr.vd)

        def __set__(self, newval):
            cdef arma.vec *vdVec
            try:
                vdVec = arma.np2vec(newval)
                self.thisptr.vd = deref(vdVec)
            finally:
                del vdVec

    property beam_ctr:
        """A vector used to re-center the beam in the chamber after the calibration and un-tilting
        transformations. It should have units of meters."""
        def __get__(self):
            return arma.vec2np(self.thisptr.beamCtr)

        def __set__(self, newval):
            cdef arma.vec *beamCtrVec
            try:
                beamCtrVec = arma.np2vec(newval)
                self.thisptr.beamCtr = deref(beamCtrVec)
            finally:
                del beamCtrVec

    def make_event(self, np.ndarray[np.double_t, ndim=2] pos, np.ndarray[np.double_t, ndim=1] en):
        """Make the electronics signals from the given track matrix.

        Parameters
        ----------
        pos : ndarray
            The simulated track positions, as (x, y, z) triples. The units should be compatible with the
            pad plane's units (probably meters).
        en : ndarray
            The energy of the simulated particle at each time step, in MeV/u.

        Returns
        -------
        dict
            A dict mapping the pad number (as int) to a generated signal (as an ndarray).

        Raises
        ------
        RuntimeError
            If the process fails for some reason.
        """
        cdef arma.mat *posMat
        cdef arma.vec *enVec
        cdef cppmap[mcopt.pad_t, arma.vec] evtmap
        res = {}

        try:
            posMat = arma.np2mat(pos)
            enVec = arma.np2vec(en)
            evtmap = self.thisptr.makeEvent(deref(posMat), deref(enVec))
        finally:
            del posMat, enVec

        cdef cppmap[mcopt.pad_t, arma.vec].iterator iter = evtmap.begin()

        while iter != evtmap.end():
            res[deref(iter).first] = arma.vec2np(deref(iter).second)
            preinc(iter)

        return res

    def make_peaks(self, np.ndarray[np.double_t, ndim=2] pos, np.ndarray[np.double_t, ndim=1] en):
        """Make the peaks table (x, y, time_bucket, amplitude, pad_number) from the simulated data.

        Parameters
        ----------
        pos : ndarray
            The simulated (x, y, z) positions. This should be in the same units as the pad plane, which
            is probably in meters.
        en : ndarray
            The simulated energies, in MeV/u.

        Returns
        -------
        ndarray
            The peaks, as described above.
        """
        cdef arma.mat *posMat
        cdef arma.vec *enVec
        cdef arma.mat peaks
        cdef np.ndarray[np.double_t, ndim=2] res
        try:
            posMat = arma.np2mat(pos)
            enVec = arma.np2vec(en)
            peaks = self.thisptr.makePeaksTableFromSimulation(deref(posMat), deref(enVec))
            res = arma.mat2np(peaks)
        finally:
            del posMat, enVec

        return res

    def make_mesh_signal(self, np.ndarray[np.double_t, ndim=2] pos, np.ndarray[np.double_t, ndim=1] en):
        """Make the simulated mesh signal, or the total across time buckets of the simulated signals.

        Parameters
        ----------
        pos : ndarray
            The simulated track positions, as (x, y, z) triples. The units should be compatible with the
            pad plane's units (probably meters).
        en : ndarray
            The energy of the simulated particle at each time step, in MeV/u.

        Returns
        -------
        ndarray
            The simulated mesh signal. The shape is (512,).
        """
        cdef arma.mat *posMat
        cdef arma.vec *enVec
        cdef arma.vec mesh
        cdef np.ndarray[np.double_t, ndim=2] res
        try:
            posMat = arma.np2mat(pos)
            enVec = arma.np2vec(en)
            mesh = self.thisptr.makeMeshSignal(deref(posMat), deref(enVec))
            res = arma.vec2np(mesh)
        finally:
            del posMat, enVec

        return res


cdef class Minimizer:
    """A Monte Carlo minimizer for particle tracks

    Parameters
    ----------
    tracker : mcopt.Tracker
        The tracker to use to simulate the tracks.
    evtgen : mcopt.EventGenerator
        The event generator to use to do the projection onto the pad plane.
    """
    cdef mcopt.MCminimizer *thisptr

    def __cinit__(self, Tracker tr, EventGenerator evtgen):
        self.thisptr = new mcopt.MCminimizer(deref(tr.thisptr), deref(evtgen.thisptr))

    def __dealloc__(self):
        del self.thisptr

    def minimize(self, np.ndarray[np.double_t, ndim=1] ctr0, np.ndarray[np.double_t, ndim=1] sigma0,
                 np.ndarray[np.double_t, ndim=2] expPos, np.ndarray[np.double_t, ndim=1] expMesh,
                 unsigned numIters=10, unsigned numPts=200, double redFactor=0.8):
        """Perform chi^2 minimization for the track.

        Parameters
        ----------
        ctr0 : ndarray
            The initial guess for the track's parameters. These are (x0, y0, z0, enu0, azi0, pol0, bmag0).
        sig0 : ndarray
            The initial width of the parameter space in each dimension. The distribution will be centered on `ctr0` with a
            width of `sig0 / 2` in each direction.
        trueValues : ndarray
            The experimental data points, as (x, y, z) triples.
        numIters : int
            The number of iterations to perform before stopping. Each iteration draws `numPts` samples and picks the best one.
        numPts : int
            The number of samples to draw in each iteration. The tracking function will be evaluated `numPts * numIters` times.
        redFactor : float
            The factor to multiply the width of the parameter space by on each iteration. Should be <= 1.
        details : bool
            Controls the amount of detail returned. If true, return the things listed below. If False, return just
            the center and the last chi^2 value.

        Returns
        -------
        ctr : ndarray
            The fitted track parameters.
        minPosChis : ndarray
            The minimum position chi^2 value at the end of each iteration.
        minEnChis : ndarray
            The minimum energy chi^2 value at the end of each iteration.
        allParams : ndarray
            The parameters from all generated tracks. There will be `numIters * numPts` rows.
        goodParamIdx : ndarray
            The row numbers in `allParams` corresponding to the best points from each iteration, i.e. the ones whose
            chi^2 values are in `minChis`.

        Raises
        ------
        RuntimeError
            If tracking fails for some reason.
        """
        cdef arma.vec *ctr0Arr
        cdef arma.vec *sigma0Arr
        cdef arma.vec *expMeshArr
        cdef arma.mat *expPosArr
        cdef mcopt.MCminimizeResult minres

        try:
            ctr0Arr = arma.np2vec(ctr0)
            sigma0Arr = arma.np2vec(sigma0)
            expPosArr = arma.np2mat(expPos)
            expMeshArr = arma.np2vec(expMesh)
            minres = self.thisptr.minimize(deref(ctr0Arr), deref(sigma0Arr), deref(expPosArr), deref(expMeshArr),
                                           numIters, numPts, redFactor)
        finally:
            del ctr0Arr, sigma0Arr, expPosArr, expMeshArr

        cdef np.ndarray[np.double_t, ndim=1] ctr = arma.vec2np(minres.ctr)
        cdef np.ndarray[np.double_t, ndim=2] allParams = arma.mat2np(minres.allParams)
        cdef np.ndarray[np.double_t, ndim=1] minPosChis = arma.vec2np(minres.minPosChis)
        cdef np.ndarray[np.double_t, ndim=1] minEnChis = arma.vec2np(minres.minEnChis)
        cdef np.ndarray[np.double_t, ndim=1] goodParamIdx = arma.vec2np(minres.goodParamIdx)

        return ctr, allParams, minPosChis, minEnChis, goodParamIdx

    def find_position_deviations(self, np.ndarray[np.double_t, ndim=2] simArr, np.ndarray[np.double_t, ndim=2] expArr):
        """Find the deviations in position between two tracks.

        Parameters
        ----------
        simArr : ndarray
            The simulated track.
        expArr : ndarray
            The experimental data.

        Returns
        -------
        devArr : ndarray
            The array of differences (or deviations).
        """
        cdef arma.mat *simMat
        cdef arma.mat *expMat
        cdef arma.mat devMat
        cdef np.ndarray[np.double_t, ndim=2] devArr

        try:
            simMat = arma.np2mat(simArr)
            expMat = arma.np2mat(expArr)
            devMat = self.thisptr.findPositionDeviations(deref(simMat), deref(expMat))
            devArr = arma.mat2np(devMat)

        finally:
            del simMat, expMat

        return devArr

    def find_energy_deviation(self, np.ndarray[np.double_t, ndim=2] simPos, np.ndarray[np.double_t, ndim=1] simEn,
                              np.ndarray[np.double_t, ndim=1] expMesh):
        """Find the deviations between the simulated track's energy and the experimental data.

        Parameters
        ----------
        sim_pos : ndarray
            The simulated track's (x, y, z) positions. The units should be compatible with the
            units of the pad plane object (probably meters).
        sim_en : ndarray
            The simulated track's energy values, in MeV/u. This should have the same number of rows
            as sim_pos.
        exp_mesh : ndarray
            The simulated track's reconstructed mesh signal. This should have a length of 512 like
            the GET electronics signals.

        Returns
        -------
        ndarray
            The deviation between the two signals, as seen by the minimizer.
        """
        cdef arma.mat *simPosMat
        cdef arma.vec *simEnVec
        cdef arma.vec *expMeshVec
        cdef arma.vec enDevVec
        cdef np.ndarray[np.double_t, ndim=1] enDev

        try:
            simPosMat = arma.np2mat(simPos)
            simEnVec = arma.np2vec(simEn)
            expMeshVec = arma.np2vec(expMesh)

            enDevVec = self.thisptr.findEnergyDeviation(deref(simPosMat), deref(simEnVec), deref(expMeshVec))
            enDev = arma.vec2np(enDevVec)

        finally:
            del simPosMat, simEnVec, expMeshVec

        return enDev
