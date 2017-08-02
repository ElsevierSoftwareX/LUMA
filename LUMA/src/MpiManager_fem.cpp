/*
 * --------------------------------------------------------------
 *
 * ------ Lattice Boltzmann @ The University of Manchester ------
 *
 * -------------------------- L-U-M-A ---------------------------
 *
 *  Copyright (C) The University of Manchester 2017
 *  E-mail contact: info@luma.manchester.ac.uk
 *
 * This software is for academic use only and not available for
 * further distribution commericially or otherwise without written consent.
 *
 */

#include "../inc/ObjectManager.h"

// ************************************************************************* //
/// \brief	Do communication required for getting off-rank markers
///
///
/// \param current grid level
void MpiManager::mpi_forceCommGather(int level) {

	// Get object manager instance
	ObjectManager *objman = ObjectManager::getInstance();

	// Declare send buffer
	std::vector<std::vector<double>> sendBuffer(num_ranks, std::vector<double>(0));

	// Pack the data to send
	int toRank, ib, m;
	for (int i = 0; i < markerCommMarkerSide[level].size(); i++) {

		// Get body ID
		ib = objman->bodyIDToIdx[markerCommMarkerSide[level][i].bodyID];

		// Only pack if body belongs to current grid level and is flexible
		if (objman->iBody[ib]._Owner->level == level && objman->iBody[ib].isFlexible) {

			// Get ID info
			toRank = markerCommMarkerSide[level][i].rankComm;
			m = markerCommMarkerSide[level][i].markerIdx;

			// Pack marker data
			sendBuffer[toRank].push_back(objman->iBody[ib].markers[m].force_xyz[eXDirection]);
			sendBuffer[toRank].push_back(objman->iBody[ib].markers[m].force_xyz[eYDirection]);
#if (L_DIMS == 3)
			sendBuffer[toRank].push_back(objman->iBody[ib].markers[m].force_xyz[eZDirection]);
#endif
		}
	}

	// Now loop through send buffer and send to appropriate ranks
	std::vector<MPI_Request> sendRequests;
	for (int toRank = 0; toRank < num_ranks; toRank++) {
		if (sendBuffer[toRank].size() > 0) {
			sendRequests.push_back(MPI_REQUEST_NULL);
			MPI_Isend(&sendBuffer[toRank].front(), sendBuffer[toRank].size(), MPI_DOUBLE, toRank, my_rank, world_comm, &sendRequests.back());
		}
	}

	// Get buffer sizes
	std::vector<int> bufferSize(num_ranks, 0);
	for (int i = 0; i < markerCommOwnerSide[level].size(); i++) {
		if (objman->iBody[objman->bodyIDToIdx[markerCommOwnerSide[level][i].bodyID]].isFlexible)
			bufferSize[markerCommOwnerSide[level][i].rankComm] += L_DIMS;
	}

	// Now create receive buffer
	std::vector<std::vector<double>> recvBuffer(num_ranks, std::vector<double>(0));

	// Now loop through and receive the buffer
	for (int fromRank = 0; fromRank < num_ranks; fromRank++) {

		// Only do if there is information to receive
		if (bufferSize[fromRank] > 0) {
			recvBuffer[fromRank].resize(bufferSize[fromRank]);
			MPI_Recv(&recvBuffer[fromRank].front(), recvBuffer[fromRank].size(), MPI_DOUBLE, fromRank, fromRank, world_comm, MPI_STATUS_IGNORE);
		}
	}

	// Index vector for looping through recvBuffer
	std::vector<int> idx(num_ranks, 0);

	// Now unpack
	int fromRank;
	for (int i = 0; i < markerCommOwnerSide[level].size(); i++) {

		// Get body idx
		ib = objman->bodyIDToIdx[markerCommOwnerSide[level][i].bodyID];

		// Only unpack if body is flexible
		if (objman->iBody[ib].isFlexible) {

			// Get ID info
			fromRank = markerCommOwnerSide[level][i].rankComm;
			m = markerCommOwnerSide[level][i].markerID;

			// Loop through and set force
			for (int d = 0; d < L_DIMS; d++)
				objman->iBody[ib].markers[m].force_xyz[d] = recvBuffer[fromRank][idx[fromRank]+d];

			// Increment
			idx[fromRank] += L_DIMS;
		}
	}

	// If sending any messages then wait for request status
	MPI_Waitall(sendRequests.size(), &sendRequests.front(), MPI_STATUS_IGNORE);
}

/// \brief	Spread new marker positions from owning rank to all other ranks
///
///
/// \param current grid level
void MpiManager::mpi_spreadNewMarkers(int level, std::vector<std::vector<int>> &markerIDs, std::vector<std::vector<std::vector<double>>> &positions, std::vector<std::vector<std::vector<double>>> &vels) {

	// Get object manager instance
	ObjectManager *objman = ObjectManager::getInstance();

	// Declare send buffers
	std::vector<int> nMarkersToSend(num_ranks, 0);
	std::vector<std::vector<int>> sendIDs(num_ranks, std::vector<int>(0));
	std::vector<std::vector<double>> sendPosAndVel(num_ranks, std::vector<double>(0.0));

	// Loop through and pack data
	for (auto ib : objman->IdxFEM) {

		// Only do if on this grid level
		if (objman->iBody[ib]._Owner->level == level) {

			// Loop through all markers (valid and invalid) on this rank
			for (size_t m = 0; m < objman->iBody[ib].markers.size(); m++) {

				// If it is not on this rank then pack into buffer
				if (objman->iBody[ib].markers[m].owningRank != my_rank) {

					// Increment the number of markers to send to this rank
					nMarkersToSend[objman->iBody[ib].markers[m].owningRank]++;

					// Pack body and marker IDs
					sendIDs[objman->iBody[ib].markers[m].owningRank].push_back(objman->iBody[ib].id);
					sendIDs[objman->iBody[ib].markers[m].owningRank].push_back(objman->iBody[ib].markers[m].id);

					// Pack position
					for (int d = 0; d < L_DIMS; d++)
						sendPosAndVel[objman->iBody[ib].markers[m].owningRank].push_back(objman->iBody[ib].markers[m].position[d]);

					// Pack position
					for (int d = 0; d < L_DIMS; d++)
						sendPosAndVel[objman->iBody[ib].markers[m].owningRank].push_back(objman->iBody[ib].markers[m].markerVel[d]);
				}
			}
		}
	}

	// To an all gather so each rank knows how many markers it is receiving from each of the other ranks
	std::vector<int> nMarkersToRecv(num_ranks, 0);
	MPI_Alltoall(&nMarkersToSend.front(), 1, MPI_INT, &nMarkersToRecv.front(), 1, MPI_INT, world_comm);

	// Loop through all sends that are required
	std::vector<MPI_Request> sendRequests;
	for (int toRank = 0; toRank < num_ranks; toRank++) {

		// Check if it has stuff to send to rank toRank
		if (nMarkersToSend[toRank] > 0) {
			sendRequests.push_back(MPI_REQUEST_NULL);
			MPI_Isend(&sendIDs[toRank].front(), sendIDs[toRank].size(), MPI_INT, toRank, my_rank, world_comm, &sendRequests.back());
			sendRequests.push_back(MPI_REQUEST_NULL);
			MPI_Isend(&sendPosAndVel[toRank].front(), sendPosAndVel[toRank].size(), MPI_DOUBLE, toRank, my_rank, world_comm, &sendRequests.back());
		}
	}

	// Declare receive buffers
	std::vector<std::vector<int>> recvIDs(num_ranks, std::vector<int>(0));
	std::vector<std::vector<double>> recvPositions(num_ranks, std::vector<double>(0.0));

	// Loop through all receives that are required
	for (int fromRank = 0; fromRank < num_ranks; fromRank++) {

		// Resize the buffer
		recvIDs[fromRank].resize(nMarkersToRecv[fromRank] * 2);
		recvPositions[fromRank].resize(nMarkersToRecv[fromRank] * 2 * L_DIMS);

		// Check if it has stuff to receive from rank i
		if (nMarkersToRecv[fromRank] > 0) {
			MPI_Recv(&recvIDs[fromRank].front(), recvIDs[fromRank].size(), MPI_INT, fromRank, fromRank, world_comm, MPI_STATUS_IGNORE);
			MPI_Recv(&recvPositions[fromRank].front(), recvPositions[fromRank].size(), MPI_DOUBLE, fromRank, fromRank, world_comm, MPI_STATUS_IGNORE);
		}
	}

	// Unpack data
	int ib;
	std::vector<double> positionVec(L_DIMS, 0.0);
	std::vector<double> velVec(L_DIMS, 0.0);
	for (int fromRank = 0; fromRank < num_ranks; fromRank++) {

		// Loop through markers which have been received from this rank
		for (int marker = 0; marker < nMarkersToRecv[fromRank]; marker++) {

			// Unpack IDs
			ib = objman->bodyIDToIdx[recvIDs[fromRank][marker*2]];
			markerIDs[ib].push_back(recvIDs[fromRank][marker*2+1]);

			// Unpack positions
			for (int d = 0; d < L_DIMS; d++) {
				positionVec[d] = recvPositions[fromRank][marker*(L_DIMS*2)+d];
				velVec[d] = recvPositions[fromRank][marker*(L_DIMS*2)+d+2];
			}

			// Push back
			positions[ib].push_back(positionVec);
			vels[ib].push_back(velVec);
		}
	}

	// If sending any messages then wait for request status
	MPI_Waitall(sendRequests.size(), &sendRequests.front(), MPI_STATUS_IGNORE);
}

// ************************************************************************** //
