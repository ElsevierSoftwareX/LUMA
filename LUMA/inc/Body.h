/*
 * --------------------------------------------------------------
 *
 * ------ Lattice Boltzmann @ The University of Manchester ------
 *
 * -------------------------- L-U-M-A ---------------------------
 *
 *  Copyright (C) 2015, 2016
 *  E-mail contact: info@luma.manchester.ac.uk
 *
 * This software is for academic use only and not available for
 * distribution without written consent.
 *
 */
#ifndef BODY_H
#define BODY_H

#include "stdafx.h"
class GridObj;
#include "PCpts.h"
#include "GridUtils.h"
#include "MarkerData.h"

/// \brief	Generic body class.
///
///			Can consist of any type of Marker so templated.
template <typename MarkerType>
class Body
{


public:

	Body(void);					// Default Constructor
	virtual ~Body(void);		// Default destructor
	Body(GridObj* g, int bodyID, PCpts* _PCpts);									// Custom constructor to build from point cloud data
	Body(GridObj* g, int bodyID, int lev, int reg, std::vector<double> &centre_point, double radius);
	Body(GridObj* g, int bodyID, int lev,
			int reg, std::vector<double> &start_position,
			double length, std::vector<double> &angles);						// Custom constructor to build filament

	// ************************ Members ************************ //

protected:
	GridObj* _Owner;					///< Pointer to owning grid
	int id;								///< Unique ID of the body
	bool closed_surface;				///< Flag to specify whether or not it is a closed surface (i.e. last marker should link to first)
	size_t owningRank;					///< ID of the rank that owns this body (for epsilon and structural calculation)
	std::vector<MarkerType> markers;	///< Array of markers which make up the body
	double spacing;						///< Reference spacing of the markers


	// ************************ Methods ************************ //

	virtual void addMarker(double x, double y, double z, int markerID);		// Add a marker (can be overrriden)
	MarkerData* getMarkerData(double x, double y, double z);				// Retireve nearest marker data
	void passToVoxelFilter(double x, double y, double z, int markerID,
		int& curr_mark, std::vector<int>& counter);							// Voxelising marker adder
	void deleteRecvLayerMarkers();											// Delete any markers which are on receiver layer
	void deleteOffRankMarkers();											// Delete any markers which don't exist on this rank

private:
	bool isInVoxel(double x, double y, double z, int curr_mark);			// Check a point is inside an existing marker voxel
	bool isVoxelMarkerVoxel(double x, double y, double z);					// Check whether nearest voxel is a marker voxel


protected:
	void buildFromCloud(PCpts *_PCpts);										// Method to build body from point cloud


};


// ************************ Implementation of Body methods ************************ //

/// Default Constructor
template <typename MarkerType>
Body<MarkerType>::Body(void)
{
	this->_Owner = nullptr;
};

/// Default destructor
template <typename MarkerType>
Body<MarkerType>::~Body(void)
{
};

/// \brief Custom constructor to call method to build from point cloud.
///
/// \param g pointer to grid which owns this body.
/// \param id indicates unique number of body in array of bodies.
/// \param _PCpts pointer to point cloud data.
template <typename MarkerType>
Body<MarkerType>::Body(GridObj* g, int bodyID, PCpts* _PCpts)
{
	// Set the body base class parameters from constructor inputs
	this->_Owner = g;
	this->id = bodyID;
	this->closed_surface = false;

	// Set the rank which owns this body
#ifdef L_BUILD_FOR_MPI
	this->owningRank = id % MpiManager::getInstance()->num_ranks;
#else
	this->owningRank = 0;
#endif

	// Call method to build from point cloud
	this->buildFromCloud(_PCpts);

	// Define spacing based on first two markers	// TODO Make spacing a marker member
	this->spacing = _Owner->dh;
};

/// \brief Custom constructor to call method to build filament.
///
/// \param g pointer to grid which owns this body.
/// \param id indicates unique number of body in array of bodies.
/// \param _PCpts pointer to point cloud data.
template <typename MarkerType>
Body<MarkerType>::Body(GridObj* g, int bodyID, int lev, int reg,
		std::vector<double> &start_position, double length, std::vector<double> &angles)
{

	// Set the body base class parameters from constructor inputs
	this->_Owner = g;
	this->id = bodyID;
	this->closed_surface = false;

	// Set the rank which owns this body
#ifdef L_BUILD_FOR_MPI
	this->owningRank = id % MpiManager::getInstance()->num_ranks;
#else
	this->owningRank = 0;
#endif

	// Get horizontal and vertical angles
	double body_angle_v = angles[0];
#if (L_DIM == 3)
	double body_angle_h = angles[1];
#else
	double body_angle_h = 0.0;
#endif

	// Compute spacing
	int numMarkers = floor(length / g->dh) + 1;
	spacing = length / (numMarkers - 1);							// Physical spacing between markers
	double spacing_h = spacing * cos(body_angle_v * L_PI / 180);	// Local spacing projected onto the horizontal plane

	// Add all markers
	for (int i = 0; i < numMarkers; i++) {
		addMarker(	start_position[0] + i * spacing_h * cos(body_angle_h * L_PI / 180.0),
					start_position[1] + i * spacing * sin(body_angle_v * L_PI / 180.0),
					start_position[2] + i * spacing_h * sin(body_angle_h * L_PI / 180.0),
					i);
	}

	// Delete markers which exist off rank
	*GridUtils::logfile << "Deleting markers which are not on this rank..." << std::endl;
	deleteOffRankMarkers();
};


/// \brief Custom constructor to call method to build filament.
///
/// \param g pointer to grid which owns this body.
/// \param id indicates unique number of body in array of bodies.
/// \param _PCpts pointer to point cloud data.
template <typename MarkerType>
Body<MarkerType>::Body(GridObj* g, int bodyID, int lev, int reg,
		std::vector<double> &centre, double radius)
{

	// Set the body base class parameters from constructor inputs
	this->_Owner = g;
	this->id = bodyID;
	this->closed_surface = false;

	// Set the rank which owns this body
#ifdef L_BUILD_FOR_MPI
	this->owningRank = id % MpiManager::getInstance()->num_ranks;
#else
	this->owningRank = 0;
#endif

	// Build sphere (3D)
#if (L_DIMS == 3)	// TODO Sort out 3D sphere builder

	// Sphere //

	// Following code for point generation on unit sphere actually seeds
	// using Fibonacci sphere technique. Code is not my own but works.
	double inc = L_PI * (3 - sqrt(5));
	double off = 2.0 / (float)L_NUM_MARKERS ;
	for (int k = 0; k < L_NUM_MARKERS; k++) {
		double y = k * off - 1 + (off / 2);
		double r = sqrt(1 - y*y);
		double phi = k * inc;

		// Add Lagrange marker to body (scale by radius)
		addMarker(centre[0] + (cos(phi)*r * radius), y*radius + centre[1], centre[2] + (sin(phi)*r*radius), isFlexible);
	}

	// Spacing (assuming all Lagrange markers are uniformly spaced)
	std::vector<double> diff;
	for (int d = 0; d < L_DIMS; d++) {
		diff.push_back ( markers[1].position[d] - markers[0].position[d] );
	}
	spacing = GridUtils::vecnorm( diff );



#else

	// Build circle (2D)
	double numMarkers = 2.0 * L_PI * radius / g->dh;
	std::vector<double> theta = GridUtils::linspace(0, 2.0 * L_PI - (2.0 * L_PI / numMarkers), numMarkers);
	for (size_t i = 0; i < theta.size(); i++) {

		// Add Lagrange marker to body
		addMarker(	centre[0] + radius * cos(theta[i]),
					centre[1] + radius * sin(theta[i]),
					centre[2],
					i );
	}

	// Spacing
	std::vector<double> diff;
	for (int d = 0; d < L_DIMS; d++) {
		diff.push_back ( markers[1].position[d] - markers[0].position[d] );
	}
	spacing = GridUtils::vecnorm( diff );
#endif

	// Delete markers which exist off rank
	*GridUtils::logfile << "Deleting markers which are not on this rank..." << std::endl;
	deleteOffRankMarkers();
};


/// \brief	Add marker to the body.
/// \param	x			global X-position of marker.
/// \param	y			global Y-position of marker.
/// \param	z 			global Z-position of marker.
/// \param markerID		ID of marker within body
template <typename MarkerType>
void Body<MarkerType>::addMarker(double x, double y, double z, int markerID)
{

	// Add a new marker object to the array
	markers.emplace_back(x, y, z, markerID, _Owner);

};
/// \brief	Delete markers which have been built but exist off rank.
template <typename MarkerType>
void Body<MarkerType>::deleteRecvLayerMarkers()
{
	// Loop through markers in body and delete ones which are on receiver layer
	int a = 0;
	do {
		// If on receiver layer then delete that marker
		if (GridUtils::isOnRecvLayer(this->markers[a].position[eXDirection], this->markers[a].position[eYDirection], this->markers[a].position[eZDirection]))
		{
			this->markers.erase(this->markers.begin() + a);
		}
		// If not, keep and move onto next one
		else {

			// Increment counter
			a++;
		}
	} while (a < static_cast<int>(this->markers.size()));
}


/// \brief	Delete markers which have been built but exist off rank.
template <typename MarkerType>
void Body<MarkerType>::deleteOffRankMarkers()
{

	// Loop through markers in body and delete ones which are not on this rank
	eLocationOnRank loc = eNone;
	int a = 0;
	do {
		// If not on rank then delete that marker
		if (!GridUtils::isOnThisRank(this->markers[a].position[eXDirection], this->markers[a].position[eYDirection], this->markers[a].position[eZDirection], &loc, this->_Owner))
		{
			this->markers.erase(this->markers.begin() + a);
		}
		// If it is, keep and move onto next one
		else {

			// Increment counter
			a++;
		}
	} while (a < static_cast<int>(this->markers.size()));
};

/*********************************************/
/// \brief	Retrieve marker data.
///
///			Return marker whose primary support data is nearest the
///			supplied global position.
///
/// \param x X-position nearest to marker to be retrieved.
/// \param y Y-position nearest to marker to be retrieved.
/// \param z Z-position nearest to marker to be retrieved.
/// \return MarkerData marker data structure returned. If no marker found, structure is marked as invalid.
template <typename MarkerType>
MarkerData* Body<MarkerType>::getMarkerData(double x, double y, double z) {

	// Get indices of voxel associated with the supplied position
	std::vector<int> vox;
	eLocationOnRank *loc = nullptr;
	if (GridUtils::isOnThisRank(x, y, z, loc, _Owner, &vox))
	{

		// Find marker whose primary support point matches these indices
		for (int i = 0; i < static_cast<int>(markers.size()); ++i) {
			if (markers[i].supp_i[0] == vox[0] &&
				markers[i].supp_j[0] == vox[1] &&
				markers[i].supp_k[0] == vox[2]) {

				// Indice represents the target ID so create new MarkerData store on the heap
				MarkerData* m_MarkerData = new MarkerData(
					markers[i].supp_i[0],
					markers[i].supp_j[0],
					markers[i].supp_k[0],
					markers[i].position[0],
					markers[i].position[1],
					markers[i].position[2],
					i
					);
				return m_MarkerData;	// Return the pointer to store information
			}

		}
	}

	// If not found then create empty MarkerData store using default constructor
	MarkerData* m_MarkerData = new MarkerData();
	return m_MarkerData;

};

/*********************************************/
/// \brief	Downsampling voxel-grid filter to take a point and add it to current body.
///
///			This method attempts to add a marker to body at the global location 
///			but obeys the rules of a voxel-grid filter to ensure markers are
///			distributed such that their spacing roughly matches the 
///			background lattice. It is usually called in side a loop and requires
///			a few extra pieces of information to be tracked throughout.
///
/// \param x desired global X-position of new marker.
/// \param y desired global Y-position of new marker.
/// \param z desired global Z-position of new marker.
/// \param curr_mark is a reference to the ID of last marker added.
///	\param counter is a reference to the total number of markers in the body.
template <typename MarkerType>
void Body<MarkerType>::passToVoxelFilter(double x, double y, double z, int markerID, int& curr_mark, std::vector<int>& counter) {

	// If point in current voxel
	if (isInVoxel(x, y, z, curr_mark)) {

		// Increment point counter
		counter[curr_mark]++;

		// Update position of marker in current voxel
		markers[curr_mark].position[0] =
			((markers[curr_mark].position[0] * (counter[curr_mark] - 1)) + x) / counter[curr_mark];
		markers[curr_mark].position[1] =
			((markers[curr_mark].position[1] * (counter[curr_mark] - 1)) + y) / counter[curr_mark];
		markers[curr_mark].position[2] =
			((markers[curr_mark].position[2] * (counter[curr_mark] - 1)) + z) / counter[curr_mark];


		// If point is in an existing voxel
	}
	else if (isVoxelMarkerVoxel(x, y, z)) {

		// Recover voxel number
		MarkerData* m_data = getMarkerData(x, y, z);
		curr_mark = m_data->ID;

		// Increment point counter
		counter[curr_mark]++;

		// Update position of marker in current voxel
		markers[curr_mark].position[0] =
			((markers[curr_mark].position[0] * (counter[curr_mark] - 1)) + x) / counter[curr_mark];
		markers[curr_mark].position[1] =
			((markers[curr_mark].position[1] * (counter[curr_mark] - 1)) + y) / counter[curr_mark];
		markers[curr_mark].position[2] =
			((markers[curr_mark].position[2] * (counter[curr_mark] - 1)) + z) / counter[curr_mark];

		delete m_data;
			
	}
	// Must be in a new marker voxel
	else {

		// Reset counter and increment voxel index
		curr_mark = static_cast<int>(counter.size());
		counter.push_back(1);

		// Create new marker as this is a new marker voxel
		addMarker(x, y, z, markerID);

	}


};

/*********************************************/
/// \brief	Determines whether a point is inside another marker's support voxel.
///
///			Typically called indirectly by the voxel-grid filter method and not directly.
///
/// \param x X-position of point.
/// \param y Y-position of point.
/// \param z Z-position of point.
/// \param curr_mark ID of the marker.
/// \return true of false
template <typename MarkerType>
bool Body<MarkerType>::isInVoxel(double x, double y, double z, int curr_mark) {

	try {

		// Try to retrieve the position of the voxel centre belonging to <curr_mark>
		// Assume that first support point is the closest to the marker position
		double vx = _Owner->XPos[markers[curr_mark].supp_i[0]];
		double vy = _Owner->YPos[markers[curr_mark].supp_j[0]];
		double vz = _Owner->ZPos[markers[curr_mark].supp_k[0]];

		// Test within
		if ((x >= vx - (_Owner->dh / 2) && x < vx + (_Owner->dh / 2)) &&
			(y >= vy - (_Owner->dh / 2) && y < vy + (_Owner->dh / 2)) &&
			(z >= vz - (_Owner->dh / 2) && z < vz + (_Owner->dh / 2))
			) return true;

		// Catch all
	}
	catch (...) {

		// If failed, marker probably doesn't exist so return false
		return false;

	}

	return false;

};

/*********************************************/
/// \brief	Determines whether a point is inside an existing marker's support voxel.
///
///			Typically called indirectly by the voxel-grid filter method and not directly.
///
/// \param x X-position of point.
/// \param y Y-position of point.
/// \param z Z-position of point.
/// \return true of false
// Returns boolean as to whether a given point is in an existing marker voxel
template <typename MarkerType>
bool Body<MarkerType>::isVoxelMarkerVoxel(double x, double y, double z) {

	// Try get the MarkerData store
	MarkerData* m_data = getMarkerData(x, y, z);

	// True if the data store is not empty
	if (m_data->ID != -1) {

		delete m_data;	// Deallocate the store before leaving scope
		return true;

	}

	delete m_data;
	return false;

};


/*********************************************/
/// \brief	Method to build a body from point cloud data.
/// \param _PCpts	point cloud data from which to build body.
// Returns boolean as to whether a given point is in an existing marker voxel
template <typename MarkerType>
void Body<MarkerType>::buildFromCloud(PCpts *_PCpts)
{
	// Declare local variables
	std::vector<int> locals;

	// Voxel grid filter //
	*GridUtils::logfile << "ObjectManagerIBB: Applying voxel grid filter..." << std::endl;

	// Place first marker
	addMarker(_PCpts->x[0], _PCpts->y[0], _PCpts->z[0], _PCpts->id[0]);

	// Increment counters
	int curr_marker = 0;
	std::vector<int> counter;
	counter.push_back(1);

	// Loop over array of points
	for (size_t a = 1; a < _PCpts->x.size(); a++)
	{
		// Pass to overridden point builder
		passToVoxelFilter(_PCpts->x[a], _PCpts->y[a], _PCpts->z[a], _PCpts->id[a], curr_marker, counter);
	}

	*GridUtils::logfile << "ObjectManager: Object represented by " << std::to_string(markers.size()) <<
		" markers using 1 marker / voxel voxelisation." << std::endl;
};

#endif

