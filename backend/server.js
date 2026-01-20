require('dotenv').config();

const express = require('express');
const cors = require('cors');
const morgan = require('morgan');
const multer = require('multer');
const { v4: uuidv4 } = require('uuid');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const archiver = require('archiver');

const app = express();
const PORT = process.env.PORT || 8080;

// --- Middleware ---
app.use(cors()); // Enable Cross-Origin Resource Sharing
app.use(morgan('dev')); // HTTP request logger
app.use(express.json()); // To parse JSON bodies
app.use(express.urlencoded({ extended: true })); // To parse URL-encoded bodies

// --- File Storage Setup with Multer ---
// This configures where uploaded files will be stored.
const storage = multer.diskStorage({
  destination: (req, file, cb) => {
    // Each upload will get a unique directory.
    const uploadId = uuidv4();
    const dir = path.join(__dirname, 'uploads', uploadId);
    fs.mkdirSync(dir, { recursive: true });
    req.uploadDir = dir; // Attach the directory to the request object
    cb(null, dir);
  },
  filename: (req, file, cb) => {
    // Use the original file name for the uploaded file.
    cb(null, file.originalname);
  },
});

const upload = multer({ storage: storage });

// --- Static File Serving ---
// Serve the processed results from the 'results' directory.
app.use('/results', express.static(path.join(__dirname, 'results')));

// --- API Routes ---

/**
 * @route POST /api/process
 * @description Handles the file upload and triggers the C++ processing script.
 * Expects a multipart/form-data request with a 'model' field containing the file.
 */
app.post('/api/process', upload.single('model'), (req, res) => {
  if (!req.file) {
    return res.status(400).json({ error: 'No file uploaded. Please upload a model file.' });
  }

  const inputDir = req.uploadDir;
  const outputDir = path.join(__dirname, 'results', path.basename(inputDir));
  
  // Create the output directory if it doesn't exist.
  if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
  }

  // Get C++ tool path from environment variables
  const cppToolPath = process.env.CPP_TOOL_PATH;
  
  if (!cppToolPath || !fs.existsSync(cppToolPath)) {
      const errorMessage = 'Processing tool not configured or not found on the server. Please ensure CPP_TOOL_PATH is set correctly in the .env file.';
      console.error(`Error: ${errorMessage}`);
      return res.status(500).json({
          error: 'Processing tool not configured on the server.',
          message: 'The server administrator needs to configure the path to the C++ executable.'
      });
  }

  console.log('--- Starting C++ Process ---');
  console.log(`Input Directory: ${inputDir}`);
  console.log(`Output Directory: ${outputDir}`);
  console.log(`Executable Path: ${cppToolPath}`);

  const args = [
    '--input_directory', inputDir,
    '--output_directory', outputDir,
  ];

  // Check for additional parameters from the request body and add them to the args array.
  if (req.body.tolerance) {
    args.push('--tolerance', req.body.tolerance);
  }
  if (req.body.instanceLimit) {
    args.push('--instance-limit', req.body.instanceLimit);
  }
  if (req.body.mergeAllGlb === 'true') {
    args.push('--merge-all-glb');
  }
  if (req.body.meshSegmentation === 'true') {
    args.push('--mesh-segmentation');
  }

  console.log('--- C++ Arguments ---');
  console.log(args);

  // Set the working directory for the C++ process to where the executable is.
  // This is crucial for the executable to find its dependent DLLs.
  const options = {
    cwd: path.dirname(cppToolPath)
  };

  const cppProcess = spawn(cppToolPath, args, options);

  // Capture standard output and standard error from the C++ process
  let stdoutData = '';
  let stderrData = '';
  cppProcess.stdout.on('data', (data) => {
    stdoutData += data.toString();
    console.log(`[C++ STDOUT]: ${data.toString()}`);
  });
  cppProcess.stderr.on('data', (data) => {
    stderrData += data.toString();
    console.error(`[C++ STDERR]: ${data.toString()}`);
  });

  // Handle the completion of the C++ process
  cppProcess.on('close', (code) => {
    console.log(`C++ process exited with code ${code}`);

    // Clean up the uploaded file's directory after processing
    fs.rm(inputDir, { recursive: true, force: true }, (err) => {
        if(err) console.error(`Failed to clean up upload directory ${inputDir}:`, err);
    });

    if (code === 0) {
      // Success. We need to find the generated tileset.json and analysis data.
      const instancedTileset = 'tileset_instanced.json';
      const nonInstancedTileset = 'tileset_non_instanced.json';
      const analysisCsv = 'instancing_analysis.csv';

      const resultUrlPath = `${path.basename(inputDir)}/`;
      let tilesetUrl = '';
      let analysisData = null;

      // Check for tileset URL
      if (fs.existsSync(path.join(outputDir, instancedTileset))) {
          tilesetUrl = `${req.protocol}://${req.get('host')}/results/${resultUrlPath}${instancedTileset}`;
      } else if (fs.existsSync(path.join(outputDir, nonInstancedTileset))) {
          tilesetUrl = `${req.protocol}://${req.get('host')}/results/${resultUrlPath}${nonInstancedTileset}`;
      }

      // Parse analysis CSV
      const analysisCsvPath = path.join(outputDir, analysisCsv);
      if (fs.existsSync(analysisCsvPath)) {
          try {
              const csvContent = fs.readFileSync(analysisCsvPath, 'utf-8');
              const lines = csvContent.trim().split('\n');
              if (lines.length > 1) {
                  const headers = lines[0].split(',');
                  const values = lines[1].split(',');
                  analysisData = headers.reduce((obj, header, index) => {
                      obj[header.trim()] = values[index].trim();
                      return obj;
                  }, {});
              }
          } catch (csvError) {
              console.error("Error reading or parsing analysis CSV:", csvError);
          }
      }

      if (tilesetUrl) {
          res.json({
            message: 'Processing successful!',
            tilesetUrl: tilesetUrl,
            analysis: analysisData,
            logs: {
                stdout: stdoutData,
                stderr: stderrData
            }
          });
      } else {
          res.status(500).json({ 
              error: 'Processing completed, but no tileset.json was found in the output.',
              logs: {
                stdout: stdoutData,
                stderr: stderrData
              }
          });
      }
    } else {
      // Failure
      res.status(500).json({
        error: 'An error occurred during model processing.',
        logs: {
            stdout: stdoutData,
            stderr: stderrData
        }
      });
    }
  });

  cppProcess.on('error', (err) => {
    console.error('Failed to start C++ process:', err);
    res.status(500).json({ error: 'Failed to start the processing tool.' });
  });
});

/**
 * @route GET /api/download/:resultId
 * @description Zips and downloads the contents of a specific result directory.
 */
app.get('/api/download/:resultId', (req, res) => {
  const { resultId } = req.params;
  const resultDir = path.join(__dirname, 'results', resultId);

  if (!fs.existsSync(resultDir)) {
    return res.status(404).send('Result not found.');
  }

  res.setHeader('Content-Type', 'application/zip');
  res.setHeader('Content-Disposition', `attachment; filename=${resultId}_results.zip`);

  const archive = archiver('zip', {
    zlib: { level: 9 } // Sets the compression level.
  });

  // Handle errors during archiving
  archive.on('error', (err) => {
    throw err;
  });

  // Pipe the archive data to the response
  archive.pipe(res);

  // Add the entire directory to the zip
  archive.directory(resultDir, false);

  // Finalize the archive (this sends the zip to the client)
  archive.finalize();
});


/**
 * @route GET /
 * @description A simple health check route.
 */
app.get('/', (req, res) => {
  res.send('Backend server is running!');
});

// --- Server Startup ---
app.listen(PORT, () => {
  console.log(`Server is running on http://localhost:${PORT}`);
  
  // Create uploads and results directories if they don't exist
  const uploadsDir = path.join(__dirname, 'uploads');
  const resultsDir = path.join(__dirname, 'results');
  if (!fs.existsSync(uploadsDir)) fs.mkdirSync(uploadsDir);
  if (!fs.existsSync(resultsDir)) fs.mkdirSync(resultsDir);
}); 