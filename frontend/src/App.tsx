import { useState, useRef } from 'react';
import './App.css';
import CesiumViewer from './components/CesiumViewer';
import FileUpload from './components/FileUpload';

// Define the shape of the component's ref handle
interface CesiumViewerRef {
  loadTileset: (url: string) => void;
}

// Define the shape of the analysis data object
interface AnalysisData {
  [key: string]: string;
}

function App() {
  const [isLoading, setIsLoading] = useState(false);
  const [infoMessage, setInfoMessage] = useState<string | null>('Welcome! Please select a GLB model to begin.');
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [analysisData, setAnalysisData] = useState<AnalysisData | null>(null);
  const [resultId, setResultId] = useState<string | null>(null);

  const cesiumViewerRef = useRef<CesiumViewerRef>(null);

  const handleUploadSuccess = (url: string, data: AnalysisData | null) => {
    setAnalysisData(data);
    
    // Extract resultId from the tileset URL
    // e.g., http://localhost:8080/results/some-uuid/tileset.json -> some-uuid
    const urlParts = url.split('/');
    if (urlParts.length > 2) {
      setResultId(urlParts[urlParts.length - 2]);
    }

    if (cesiumViewerRef.current) {
      cesiumViewerRef.current.loadTileset(url);
      // The info message is cleared after a short delay to allow the "Loading model..." message to be seen.
      setTimeout(() => setInfoMessage(null), 3000);
    }
  };

  return (
    <>
      <div className="sidebar">
        <h1>Gltf Instancing Tool</h1>
        
        <FileUpload 
          onUploadSuccess={handleUploadSuccess}
          setInfoMessage={setInfoMessage}
          setErrorMessage={setErrorMessage}
          setIsLoading={setIsLoading}
        />

        <div className="status-section">
          <h2>Status</h2>
          {isLoading && <div className="loader"></div>}
          
          {infoMessage && !errorMessage && (
            <div className="status-message info">{infoMessage}</div>
          )}

          {errorMessage && (
            <div className="status-message error">{errorMessage}</div>
          )}
          
          {analysisData && (
            <div className="analysis-results">
              <h4>Analysis Results</h4>
              <ul>
                {Object.entries(analysisData).map(([key, value]) => (
                  <li key={key}>
                    <strong>{key.replace(/_/g, ' ')}:</strong> {value}
                  </li>
                ))}
              </ul>
              {resultId && (
                <a 
                  href={`http://localhost:8080/api/download/${resultId}`} 
                  className="download-button"
                  download
                >
                  Download Results (.zip)
                </a>
              )}
            </div>
          )}
        </div>
      </div>
      <div className="main-content">
        <CesiumViewer ref={cesiumViewerRef} />
      </div>
    </>
  );
}

export default App;
