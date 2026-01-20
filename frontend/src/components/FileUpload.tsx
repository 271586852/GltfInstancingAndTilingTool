import React, { useState } from 'react';
import axios, { AxiosError } from 'axios';

interface AnalysisData {
    [key: string]: string;
}

interface FileUploadProps {
  onUploadSuccess: (url: string, data: AnalysisData | null) => void;
  setInfoMessage: (message: string | null) => void;
  setErrorMessage: (message: string | null) => void;
  setIsLoading: (isLoading: boolean) => void;
}

interface ErrorResponseData {
  error: string;
  logs?: {
    stdout: string;
    stderr: string;
  };
}

const FileUpload: React.FC<FileUploadProps> = ({ 
  onUploadSuccess, 
  setInfoMessage, 
  setErrorMessage,
  setIsLoading 
}) => {
  const [selectedFile, setSelectedFile] = useState<File | null>(null);
  const [tolerance, setTolerance] = useState<number>(0.0);
  const [instanceLimit, setInstanceLimit] = useState<number>(2);
  const [mergeAllGlb, setMergeAllGlb] = useState<boolean>(false);
  const [meshSegmentation, setMeshSegmentation] = useState<boolean>(false);

  const handleFileChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    if (event.target.files && event.target.files[0]) {
      const file = event.target.files[0];
      if (file.name.toLowerCase().endsWith('.glb')) {
        setSelectedFile(file);
        setErrorMessage(null); // Clear previous errors
      } else {
        setSelectedFile(null);
        setErrorMessage('Please select a .glb file.');
      }
    }
  };

  const handleProcessClick = async () => {
    if (!selectedFile) {
      setErrorMessage('No file selected.');
      return;
    }

    setIsLoading(true);
    setInfoMessage('Uploading and processing... This may take a moment.');
    setErrorMessage(null);

    const formData = new FormData();
    formData.append('model', selectedFile);
    formData.append('tolerance', tolerance.toString());
    formData.append('instanceLimit', instanceLimit.toString());
    if (mergeAllGlb) {
      formData.append('mergeAllGlb', 'true');
    }
    if (meshSegmentation) {
      formData.append('meshSegmentation', 'true');
    }

    try {
      const response = await axios.post('http://localhost:8080/api/process', formData, {
        headers: {
          'Content-Type': 'multipart/form-data',
        },
      });
      
      onUploadSuccess(response.data.tilesetUrl, response.data.analysis);
      setInfoMessage('Processing successful! Loading model...');

    } catch (error: unknown) {
        console.error('Error during processing:', error);
        let detailedError = 'An unknown error occurred.';

        if (axios.isAxiosError(error)) {
            const axiosError = error as AxiosError<ErrorResponseData>;
            if (axiosError.response) {
                // The request was made and the server responded with a status code
                // that falls out of the range of 2xx
                const logs = axiosError.response.data?.logs;
                const logMessage = logs ? `\n\n--- C++ Tool Logs ---\nSTDERR:\n${logs.stderr}\nSTDOUT:\n${logs.stdout}` : '';
                detailedError = `Server Error ${axiosError.response.status}: ${axiosError.response.data?.error || 'Internal Server Error'}${logMessage}`;
            } else if (axiosError.request) {
                // The request was made but no response was received
                detailedError = 'No response from server. Is the backend running?';
            } else {
                // Something happened in setting up the request that triggered an Error
                detailedError = `Request setup error: ${axiosError.message}`;
            }
        } else if (error instanceof Error) {
            detailedError = error.message;
        }

        setErrorMessage(detailedError);
        setInfoMessage(null);
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="upload-section">
      <div className="file-input-wrapper">
        <label htmlFor="file-upload" className="file-label">
          {selectedFile ? 'Change GLB File' : 'Select GLB File'}
        </label>
        <input
          id="file-upload"
          type="file"
          accept=".glb"
          onChange={handleFileChange}
        />
        {selectedFile && <p className="file-name">{selectedFile.name}</p>}
      </div>

      <button
        className="process-button"
        onClick={handleProcessClick}
        disabled={!selectedFile}
      >
        Process Model
      </button>

      <div className="options-section">
        <h3>Processing Options</h3>
        <div className="option-item">
            <label htmlFor="tolerance">Geometry Tolerance</label>
            <input 
                type="number" 
                id="tolerance" 
                value={tolerance}
                onChange={e => setTolerance(parseFloat(e.target.value) || 0.0)}
                step="0.001"
            />
        </div>
        <div className="option-item">
            <label htmlFor="instanceLimit">Instance Limit</label>
            <input 
                type="number" 
                id="instanceLimit" 
                value={instanceLimit}
                onChange={e => setInstanceLimit(parseInt(e.target.value) || 2)}
                min="2"
            />
        </div>
        <div className="option-item checkbox-item">
            <input 
                type="checkbox" 
                id="mergeAllGlb" 
                checked={mergeAllGlb}
                onChange={e => setMergeAllGlb(e.target.checked)}
            />
            <label htmlFor="mergeAllGlb">Merge All GLB Outputs</label>
        </div>
        <div className="option-item checkbox-item">
            <input 
                type="checkbox" 
                id="meshSegmentation" 
                checked={meshSegmentation}
                onChange={e => setMeshSegmentation(e.target.checked)}
            />
            <label htmlFor="meshSegmentation">Segment Each Mesh</label>
        </div>
      </div>
    </div>
  );
};

export default FileUpload; 