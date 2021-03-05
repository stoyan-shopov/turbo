/*
 * Copyright (C) 2021 Stoyan Shopov <stoyan.shopov@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#pragma once
#include <memory>

#include <QHash>
#include <QString>
#include <QFileInfo>
#include <QDateTime>

#include "source-file-data.hxx"

/* Note: using a cache for the source code files is not really helpful for the source view, because
 * refreshing the source code view is dominated by rendering the generated html, and not by reading
 * the source code file and generating an html document for it. However, a cache for the source code
 * files can be helpful when displaying large disassembly listings. */
class SourceFilesCache
{
public:
	SourceFilesCache(){}
	struct SourceFileCacheData
	{
		/* The name of the source file, by which it can be accessed in the filesystem. This may be
		 * different from the filename initially supplied, e.g., in an MSYS2 environment. */
		QString				filesystemFileName;
		QDateTime			lastModifiedDateTime;
		std::shared_ptr<const QString>	htmlDocument;
		QStringList			sourceCodeTextlines;
	};
	std::shared_ptr<const SourceFileCacheData> getSourceFileCacheData(const QString & sourceFileName, QString &errorMessage);
	void setSourceFileData(std::shared_ptr<const QHash<QString /* gdb reported full file name */, SourceFileData>> sourceFileData)
	{ this->sourceFileData = sourceFileData; }
private:
	std::shared_ptr<const QHash<QString /* gdb reported full file name */, SourceFileData>> sourceFileData;
	QHash<QString /* source file name */, std::shared_ptr<const struct SourceFileCacheData> /* source file data */> sourceFileCacheData;
};

