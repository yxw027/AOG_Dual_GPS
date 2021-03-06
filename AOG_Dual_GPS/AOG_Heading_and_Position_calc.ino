void headingRollCalc() {

	rollPresent = false;
	dualGPSHeadingPresent = false;
	filterGPSpos = false;

	if (existsUBXRelPosNED) {
		//check if all values are vaild
		if (bitRead(UBXRelPosNED.flags, 0)) {//gnssFixOK?
			if (bitRead(UBXRelPosNED.flags, 2)) {//1 if relative position components and moving baseline are valid
			//RelPosNED OK: heading + roll calc												 
				
				if ((UBXRelPosNED.relPosLength > (GPSSet.AntDist / GPSSet.AntDistDeviationFactor)) && (UBXRelPosNED.relPosLength < (GPSSet.AntDist * GPSSet.AntDistDeviationFactor)))
				{
					//check if vector lenght between antennas is in range = indicator of heading/roll quality

					dualAntNoValue = 0;//reset watchdog

					if ((UBXRelPosNED.relPosLength > (GPSSet.AntDist / ((GPSSet.AntDistDeviationFactor / 3) + 0.667))) && (UBXRelPosNED.relPosLength < (GPSSet.AntDist * ((GPSSet.AntDistDeviationFactor / 3) + 0.667))))
					{	//check if vector lenght between antennas is in range = indicator of heading/roll quality							
						//for roll calc only 1/3 deviation !!
			//signal perfect: deviation less than 1/3

						headVarProcess = VarProcessMedi;  //set Kalman filter
						rollVarProcess = VarProcessFast; //set Kalman filter

						if (((GPSSet.headingAngleCorrection > 70) && (GPSSet.headingAngleCorrection < 110))
							|| ((GPSSet.headingAngleCorrection > 250) && (GPSSet.headingAngleCorrection < 290)))
						{//ant left+right: headingCorrectionAngle 90 or 270
				//roll
							roll = (atan2((double(UBXRelPosNED.relPosD) + (double(UBXRelPosNED.relPosHPD) / 100)), GPSSet.AntDist)) / PI180;
							roll -= GPSSet.rollAngleCorrection;
							noRollCount = 0;
							if (debugmodeHeading) { Serial.print("roll calc: "); Serial.print(roll); }
						}
						if (debugmodeFilterPos) { Serial.println("perfect Signal"); }
					}
					else
			//signal medium
					{
						headVarProcess = VarProcessVerySlow; //set Kalman filter
						rollVarProcess = VarProcessVerySlow; //roll slowly to 0
						roll = 0.0;
						noRollCount++;
						//start filter GPS pos, set Kalman values
						if (GPSSet.filterGPSposOnWeakSignal == 1) {
							filterGPSpos = true;
							latVarProcess = VarProcessVeryFast; //set Kalman filter
							lonVarProcess = VarProcessVeryFast;
						}

						if (debugmodeFilterPos) { Serial.println("medium Signal"); }
						if (debugmode) {
							Serial.println("poor quality of GPS signal: NO roll calc, heading calc OK");
							Serial.print("Antenna distance set: "); Serial.print(GPSSet.AntDist); Serial.print("  Ant. dist from GPS: "); Serial.println(UBXRelPosNED.relPosLength);
						}
					}//end of deviation good or perfect					
				//heading
					headRingCount = (headRingCount + 1) % GPSHeadingArraySize;
					GPSHeading[headRingCount] = double(UBXRelPosNED.relPosHeading) * 0.00001;
					GPSHeading[headRingCount] = GPSHeading[headRingCount] + GPSSet.headingAngleCorrection;
					if (GPSHeading[headRingCount] >= 360) { GPSHeading[headRingCount] -= 360; }
					if (GPSHeading[headRingCount] < 0) { GPSHeading[headRingCount] += 360; }
					//go to cos for filtering to avoid 360-0° jump
					double cosHead = cos((GPSHeading[headRingCount] * PI180));
					if (debugmodeHeading) { Serial.print("heading calc: "); Serial.print(GPSHeading[headRingCount]); }
					//Kalman filter heading
					headK = cosHead;//input
					headPc = headP + headVarProcess;
					headG = headPc / (headPc + headVar);
					headP = (1 - headG) * headPc;
					headXp = headXe;
					headZp = headXp;
					headXe = (headG * (headK - headZp)) + headXp;//result
				//go back to degree
					if (GPSHeading[headRingCount] <= 180)
						{GPSHeading[headRingCount] = acos(headXe) / PI180;}
						else { GPSHeading[headRingCount] = 360 - (acos(headXe) / PI180); }

					dualGPSHeadingPresent = true;
					noHeadingCount = 0;

					if (debugmodeHeading) { Serial.print("heading filterd: "); Serial.print(GPSHeading[headRingCount]); }
					//filter roll
					rollK = roll;//input
					rollPc = rollP + rollVarProcess;
					rollG = rollPc / (rollPc + rollVar);
					rollP = (1 - rollG) * rollPc;
					rollXp = rollXe;
					rollZp = rollXp;
					rollXe = (rollG * (rollK - rollZp)) + rollXp;//result
					roll = rollXe;
					if (noRollCount < 8) { rollPresent = true; }
					else { noRollCount = 20; rollPresent = false;}//prevent overflow
					if (debugmodeHeading) {
						Serial.print(" roll filtered: "); Serial.println(roll);
						Serial.print("Antenna distance set: "); Serial.print(GPSSet.AntDist); Serial.print("  Ant. dist from GPS: "); Serial.println(UBXRelPosNED.relPosLength);
					}

				}
		//very poor signal quality: send only position
				else {
					noHeadingCount++;
					if (noHeadingCount < 4) { dualGPSHeadingPresent = true; }//use last heading 3x to calc virt position to avoid jumps
					if (GPSSet.filterGPSposOnWeakSignal == 1) {
						filterGPSpos = true;
						latVarProcess = VarProcessFast; //set Kalman filter
						lonVarProcess = VarProcessFast;
					}
					if (debugmodeFilterPos) { Serial.println("very weak Signal"); }
					if (debugmode || debugmodeHeading) {
						Serial.println("poor quality of GPS signal: NO heading/roll calc");
						Serial.print("Antenna distance set: "); Serial.print(GPSSet.AntDist); Serial.print("  Ant. dist from GPS: "); Serial.println(UBXRelPosNED.relPosLength);
					}
				}

			}
		}
		else { if (debugmode) { Serial.println("UBX RelPosNED flag: relative position not valid ->  NO heading + roll calc"); } }
	}
	else { if (debugmode) { Serial.println("UBX RelPosNED flag: DGPS fix not valid ->  NO heading + roll calc"); } }
}

//-------------------------------------------------------------------------------------------------

void virtualAntennaPoint() {
	double virtLatRad = double(UBXPVT1[UBXRingCount1].lat) / 10000000 * PI180;
	double virtLonRad = double(UBXPVT1[UBXRingCount1].lon) / 10000000 * PI180;
	double virtLatRadTemp = 0.0, virtAntHeadingDiff = 0.0, headingRad = 0.0, WayByRadius = 0.0;

	double toRight = 0, rollAbs = 0;
	bool rollNeg = false;
	virtAntPosPresent = false;

	if (rollPresent && (GPSSet.GPSPosCorrByRoll == 1)) { //calculate virtual Antenna point using roll
		toRight = tan((roll * PI180)) * GPSSet.AntHight;
		if (debugmodeVirtAnt) {
			Serial.print("roll degr: "); Serial.print(roll, 2); Serial.print(" to right by roll: ");
			Serial.println(toRight);
		}
		toRight = toRight - GPSSet.virtAntRight;
	}
	else {
		toRight = 0 - GPSSet.virtAntRight;
	}

	if (GPSSet.virtAntForew != 0) {
		if (toRight != 0) {
			//shift Antenna foreward and to the right: direction to move poit: heading-atan(foreward/right)
			virtAntHeadingDiff = (atan2(toRight, GPSSet.virtAntForew)) / PI180;
			//distance to move  cm  / mean radius of earth cm (WGS84 6.371.000,8m)
			double distAnt = sqrt((toRight * toRight) + (GPSSet.virtAntForew * GPSSet.virtAntForew));
			WayByRadius = distAnt / 637100080;
			if (debugmodeVirtAnt) {
				Serial.print("virt Ant: real dist to move"); Serial.print(distAnt);
				Serial.print(" WayByRadius: "); Serial.println(WayByRadius, 10);
				Serial.print(" angle corr by: "); Serial.println(virtAntHeadingDiff, 2);
			}
		}
		else {
			//shift Antenna only foreward
			virtAntHeadingDiff = 0.0;
			//distance to move  cm  / mean radius of earth cm (WGS84 6.371.000,8m)
			WayByRadius = GPSSet.virtAntForew / 637100080;

		}
	}
	else {
		if (toRight != 0) {
			//shift Antenna only right (needed for roll compensation)
			virtAntHeadingDiff = 90;
			WayByRadius = toRight / 637100080;
			if (debugmodeVirtAnt) {
				Serial.print("effective dist to move virt Ant: "); Serial.print(toRight);
				Serial.print(" WayToRaduis: "); Serial.println(WayByRadius, 12);
			}
		}
	}//direction+way



	if ((virtLatRad != 0.0) && (virtLonRad != 0.0)) {
		//all calculations in radians:  decimal * PI / 180
		headingRad = GPSHeading[headRingCount] + virtAntHeadingDiff;
		if (headingRad > 360) { headingRad -= 360; }
		else {
			if (headingRad < 0) { headingRad += 360; }
		}
		headingRad *= PI180;
		virtLatRadTemp = asin((sin(virtLatRad) * cos(WayByRadius)) + (cos(virtLatRad) * sin(WayByRadius) * cos(headingRad)));
		virtLonRad = virtLonRad + atan2((sin(headingRad) * sin(WayByRadius) * cos(virtLatRad)), (cos(WayByRadius) - (sin(virtLatRad) * sin(virtLatRadTemp))));
		virtLatRad = virtLatRadTemp;
		//radians to dec
		virtLat = virtLatRad / PI180;
		virtLon = virtLonRad / PI180;

		virtAntPosPresent = true;

		if (debugmodeVirtAnt) {
			Serial.println("Virtual Antenna point calc:");
			Serial.print("UBX1 Lat: "); Serial.print((double(UBXPVT1[UBXRingCount1].lat) / 10000000), 7); Serial.print(" virtLat: "); Serial.print(virtLat, 7);
			Serial.print("UBX1 Lon: "); Serial.print((double(UBXPVT1[UBXRingCount1].lon) / 10000000), 7); Serial.print(" virtLon: "); Serial.println(virtLon, 7);
			Serial.print("virt Point head: "); Serial.print((headingRad), 3); Serial.print(" GPS head: "); Serial.print(GPSHeading[headRingCount], 3);
			Serial.print(" roll: "); Serial.println(roll, 3);
		}
	}
	else { if (debugmode||debugmodeVirtAnt) { Serial.println("No virtual Antenna point: lat/lon = 0"); } }
}

//-------------------------------------------------------------------------------------------------


void filterPosition() {
	//input to the kalman filter
	if (virtAntPosPresent) { latK = virtLat; lonK = virtLon; }
	else { latK = long(UBXPVT1[UBXRingCount1].lat)* 0.0000001;lonK = long(UBXPVT1[UBXRingCount1].lon) * 0.0000001; Serial.println("use UBX for pos");}

	if (debugmodeHeading || debugmodeFilterPos) { Serial.print(" lat: "); Serial.print(latK, 7); }

	//Kalman filter
	latPc = latP + latVarProcess;
	latG = latPc / (latPc + latVar);
	latP = (1 - latG) * latPc;
	latXp = latXe;
	latZp = latXp;
	latXe = (latG * (latK - latZp)) + latXp;

	virtLat = latXe;//result

	//Kalman filter
	lonPc = lonP + lonVarProcess;
	lonG = lonPc / (lonPc + lonVar);
	lonP = (1 - lonG) * lonPc;
	lonXp = lonXe;
	lonZp = lonXp;
	lonXe = (lonG * (lonK - lonZp)) + lonXp;

	virtLon = lonXe;//result

	if (debugmodeHeading || debugmodeFilterPos) {
		Serial.print(" lat filtered: "); Serial.print(virtLat, 7);
		Serial.print(" lon: "); Serial.print(lonK, 7); Serial.print(" lon filtered: "); Serial.println(virtLon, 7);
	}
}
