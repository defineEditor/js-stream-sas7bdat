import DatasetReadStat from './class/datasetReadStat';
import DatasetSas7BDat from './class/datasetSas7BDat';
import DatasetStata from './class/datasetStata';
import DatasetSpss from './class/datasetSpss';
import { detectReadStatBindingFormat } from './class/readStatBindings';

export function createDatasetReader(
    filePath: string,
    options?: ConstructorParameters<typeof DatasetReadStat>[1],
): DatasetReadStat {
    const format = options?.format ?? detectReadStatBindingFormat(filePath);

    if (format === 'sas7bdat') {
        return new DatasetSas7BDat(filePath, options);
    }
    if (format === 'dta') {
        return new DatasetStata(filePath, options);
    }

    return new DatasetSpss(filePath, options);
}

export default DatasetSas7BDat;
export { DatasetReadStat, DatasetSas7BDat, DatasetStata, DatasetSpss };
