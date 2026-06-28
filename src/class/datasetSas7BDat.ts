import DatasetReadStat, { DatasetReadStatOptions } from './datasetReadStat';

class DatasetSas7BDat extends DatasetReadStat {
    constructor(
        filePath: string,
        options?: Omit<DatasetReadStatOptions, 'format'>,
    ) {
        super(filePath, { ...options, format: 'sas7bdat' });
    }
}

export default DatasetSas7BDat;
export { DatasetSas7BDat };
