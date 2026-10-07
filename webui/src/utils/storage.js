import axios from 'axios';
import { sampleProjects } from '../../examples/import-export/sample_proj';
import { API_URL } from './constants';

const pako = require('pako');

// const STORAGE_KEY = 'pixelart-react-v3-0-0';

/*
 *  Storage data structure
 *
 *  {
 *   stored: [
 *     { frames: [],paletteGridData, cellSize, columns, rows, animate},
 *     { frames: [],paletteGridData, cellSize, columns, rows, animate},
 *     ...
 *   ]
 *   current: position
 *  }
 *
 */

async function saveDataToStorage(data) {
  try {
    const compre = pako.gzip(Buffer.from(data));
    const response = await axios.post(`${API_URL}/projects`, compre);
    return response.status === 200 ? response.data : false;
  } catch (e) {
    return false;
  }
}

/*
  Storage initialization
*/
export function initStorage() {
  saveDataToStorage(
    JSON.stringify({
      stored: sampleProjects, // Load an example project data by default
      current: 0
    })
  );
}

/*
  Get stored data from the storage
*/
export async function getDataFromStorage() {
  try {
    const response = await axios.get(`${API_URL}/projects`);
    return Object.keys(response.data).length > 0 ? response.data : false;
  } catch (e) {
    console.log(e);
    return false;
  }
}

/*
  Save a project into the stored data collection
*/
export async function saveProjectToStorage(projectData) {
  try {
    let dataStored = await getDataFromStorage();
    if (dataStored) {
      dataStored.stored.push(projectData);
      dataStored.current = dataStored.stored.length - 1;
    } else {
      dataStored = {
        stored: [projectData],
        current: 0
      };
    }
    await saveDataToStorage(JSON.stringify(dataStored));
    return true;
  } catch (e) {
    return false; // There was an error
  }
}

/*
  Remove a project from the stored data collection
*/
export async function removeProjectFromStorage(indexToRemove) {
  const dataStored = await getDataFromStorage();
  if (dataStored) {
    let newCurrent = 0;
    dataStored.stored.splice(indexToRemove, 1);
    if (dataStored.stored.length === 0) {
      newCurrent = -1; // Empty collection
    } else if (dataStored.current > indexToRemove) {
      newCurrent = dataStored.current - 1; // Current is greater than the one to remove
    }
    dataStored.current = newCurrent;
    return saveDataToStorage(JSON.stringify(dataStored));
  }
  return false; // There was an error if it reaches this code
}

/*
  Returns the export code
*/
export function generateExportString(projectData) {
  try {
    return JSON.stringify(projectData);
  } catch (e) {
    return 'Sorry, there was an error';
  }
}

/*
  Returns project data ready from a exported data string
*/
export function exportedStringToProjectData(projectData) {
  if (projectData === '') {
    return false;
  }
  try {
    return JSON.parse(projectData);
  } catch (e) {
    return false;
  }
}
