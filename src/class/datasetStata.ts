import DatasetReadStat, { DatasetReadStatOptions } from './datasetReadStat';

class DatasetStata extends DatasetReadStat {
    constructor(
        filePath: string,
        options?: Omit<DatasetReadStatOptions, 'format'>,
    ) {
        super(filePath, { ...options, format: 'dta' });
    }
}

export default DatasetStata;
export { DatasetStata };
